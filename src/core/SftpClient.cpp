#include "core/SftpClient.h"

#include "core/SshSession.h"

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include <fcntl.h>

#include <algorithm>
#include <chrono>

#include <memory>
#include <random>
#include <utility>

namespace {

using ssh_session_detail::Connect;
using ssh_session_detail::ConnectionPool;
using ssh_session_detail::PoolKey;
using ssh_session_detail::SshError;
using ssh_session_detail::SshSessionPtr;

struct SftpSessionDeleter {
  void operator()(sftp_session s) const { sftp_free(s); }
};
struct SftpFileDeleter {
  void operator()(sftp_file f) const { sftp_close(f); }
};
struct SftpAttrDeleter {
  void operator()(sftp_attributes a) const { sftp_attributes_free(a); }
};

using SftpSessionPtr = std::unique_ptr<sftp_session_struct, SftpSessionDeleter>;
using SftpFilePtr = std::unique_ptr<sftp_file_struct, SftpFileDeleter>;
using SftpAttrPtr = std::unique_ptr<sftp_attributes_struct, SftpAttrDeleter>;

wxString JoinRemote(const wxString &dir, const wxString &name) {
  return dir.EndsWith("/") ? dir + name : dir + "/" + name;
}

// Expands a leading `$HOME`/`~` against the remote home directory.
wxString ExpandHome(const wxString &path, const wxString &home) {
  if (path == "~" || path == "$HOME") {
    return home;
  }
  if (path.StartsWith("~/")) {
    return JoinRemote(home, path.Mid(2));
  }
  if (path.StartsWith("$HOME/")) {
    return JoinRemote(home, path.Mid(6));
  }
  return path;
}

// An open connection: the SSH session, its SFTP channel, and the remote home.
struct Connection {
  SshSessionPtr ssh;
  SftpSessionPtr sftp; // Freed before `ssh` (members go in reverse order)
  wxString home;
  // When it went back to the pool.
  std::chrono::steady_clock::time_point lastUsed;

  // The session is connected and its SFTP channel is open. A call that timed
  // out can leave a connection that still passes this; see WithConnection().
  bool IsHealthy() const {
    return ssh && sftp && ssh_is_connected(ssh.get()) &&
           ssh_channel_is_open(sftp->channel) &&
           !ssh_channel_is_eof(sftp->channel);
  }
};
using ConnectionPtr = std::unique_ptr<Connection>;

// Never freed: at exit the sockets close with the process.
ConnectionPool<Connection> &Pool() {
  static auto *pool = new ConnectionPool<Connection>;
  return *pool;
}

StatusOr<ConnectionPtr> NewConnection(const wxString &host,
                                      const wxString &user) {
  auto session = Connect(host, user);
  if (!session.ok()) {
    return session.status();
  }
  auto conn = std::make_unique<Connection>();
  conn->ssh = std::move(session.value());
  conn->sftp.reset(sftp_new(conn->ssh.get()));
  if (!conn->sftp || sftp_init(conn->sftp.get()) != SSH_OK) {
    return SshError("Could not start the SFTP subsystem", conn->ssh.get());
  }
  if (char *real = sftp_canonicalize_path(conn->sftp.get(), ".")) {
    conn->home = wxString::FromUTF8(real);
    ssh_string_free_char(real);
  }
  return conn;
}

// Runs `fn(Connection &)` on a connection to `host`. Only a connection whose
// call worked goes back to the pool: after a failure (a timeout can leave the
// SFTP stream out of step) the next call connects again, which is cheap next
// to reusing a broken connection. A connection from the pool may have been
// closed by the host while it was idle; when `fn` fails and the connection is
// not healthy any more, it runs once more on a new connection.
template <typename Result, typename Fn>
Result WithConnection(const wxString &host, const wxString &user, Fn &&fn) {
  const wxString key = PoolKey(host, user);
  if (ConnectionPtr pooled = Pool().Take(key)) {
    Result result = fn(*pooled);
    if (result.ok()) {
      Pool().Return(key, std::move(pooled));
      return result;
    }
    if (pooled->IsHealthy()) {
      return result;
    }
  }
  auto fresh = NewConnection(host, user);
  if (!fresh.ok()) {
    return fresh.status();
  }
  Result result = fn(*fresh.value());
  if (result.ok()) {
    Pool().Return(key, std::move(fresh.value()));
  }
  return result;
}

bool IsRegularFile(sftp_session sftp, const wxString &path, uint64_t *size) {
  SftpAttrPtr attrs{sftp_stat(sftp, path.ToStdString(wxConvUTF8).c_str())};
  if (!attrs || attrs->type != SSH_FILEXFER_TYPE_REGULAR) {
    return false;
  }
  *size = attrs->size;
  return true;
}

// Removes the folder `path` and what is in it. A symbolic link inside is
// removed, not followed.
Status RemoveRecursive(sftp_session sftp, ssh_session session,
                       const std::string &path, int depth) {
  constexpr int kMaxDepth = 16;
  if (depth > kMaxDepth) {
    return Status::Error("The folder is nested too deeply");
  }
  sftp_dir dir = sftp_opendir(sftp, path.c_str());
  if (dir == nullptr) {
    return SshError(
        wxString::Format("Could not open %s", wxString::FromUTF8(path)),
        session);
  }
  struct Entry {
    std::string name;
    bool isDir;
  };
  std::vector<Entry> entries;
  while (sftp_attributes attrs = sftp_readdir(sftp, dir)) {
    const std::string name = attrs->name != nullptr ? attrs->name : "";
    const bool isDir = attrs->type == SSH_FILEXFER_TYPE_DIRECTORY;
    sftp_attributes_free(attrs);
    if (!name.empty() && name != "." && name != "..") {
      entries.push_back({name, isDir});
    }
  }
  sftp_closedir(dir);

  for (const Entry &entry : entries) {
    const std::string child = path + "/" + entry.name;
    if (entry.isDir) {
      if (Status st = RemoveRecursive(sftp, session, child, depth + 1);
          !st.ok()) {
        return st;
      }
    } else if (sftp_unlink(sftp, child.c_str()) != SSH_OK) {
      return SshError(
          wxString::Format("Could not remove %s", wxString::FromUTF8(child)),
          session);
    }
  }
  if (sftp_rmdir(sftp, path.c_str()) != SSH_OK) {
    return SshError(
        wxString::Format("Could not remove %s", wxString::FromUTF8(path)),
        session);
  }
  return Status::Ok();
}

} // namespace

StatusOr<SftpClient::RemoteFile>
SftpClient::ReadFile(const wxString &host, const wxString &user,
                     const wxString &path,
                     const std::vector<wxString> &searchDirs, size_t maxBytes) {
  return WithConnection<StatusOr<SftpClient::RemoteFile>>(
      host, user, [&](Connection &conn) -> StatusOr<SftpClient::RemoteFile> {
        sftp_session sftp = conn.sftp.get();
        ssh_session session = conn.ssh.get();
        const wxString &home = conn.home;

        std::vector<wxString> candidates;
        if (path.StartsWith("/")) {
          candidates.push_back(path);
        } else if (path == "~" || path.StartsWith("~/") || path == "$HOME" ||
                   path.StartsWith("$HOME/")) {
          candidates.push_back(ExpandHome(path, home));
        } else {
          for (const wxString &dir : searchDirs) {
            candidates.push_back(JoinRemote(ExpandHome(dir, home), path));
          }
          if (!home.empty()) {
            candidates.push_back(JoinRemote(home, path));
          }
        }

        for (const wxString &candidate : candidates) {
          uint64_t size = 0;
          if (!IsRegularFile(sftp, candidate, &size)) {
            continue;
          }
          if (size > maxBytes) {
            return Status::Error(wxString::Format(
                "%s is too large to open (%llu bytes)", candidate,
                static_cast<unsigned long long>(size)));
          }

          SftpFilePtr file{sftp_open(
              sftp, candidate.ToStdString(wxConvUTF8).c_str(), O_RDONLY, 0)};
          if (!file) {
            return SshError(wxString::Format("Could not open %s", candidate),
                            session);
          }

          RemoteFile result;
          result.path = candidate;
          result.content.reserve(static_cast<size_t>(size));
          char buffer[32 * 1024];
          for (;;) {
            const ssize_t n = sftp_read(file.get(), buffer, sizeof(buffer));
            if (n == 0) {
              break;
            }
            if (n < 0) {
              return SshError(wxString::Format("Could not read %s", candidate),
                              session);
            }
            result.content.append(buffer, static_cast<size_t>(n));
            if (result.content.size() > maxBytes) {
              return Status::Error(
                  wxString::Format("%s is too large to open", candidate));
            }
          }
          return result;
        }

        return Status::Error(
            wxString::Format("File not found on %s: %s", host, path));
      });
}

Status SftpClient::WriteFile(const wxString &host, const wxString &user,
                             const wxString &path, const std::string &content) {
  return WithConnection<Status>(host, user, [&](Connection &conn) -> Status {
    sftp_session sftp = conn.sftp.get();
    ssh_session session = conn.ssh.get();

    // Replace the real file, not a symlink pointing at it.
    std::string target = path.ToStdString(wxConvUTF8);
    if (char *real = sftp_canonicalize_path(sftp, target.c_str())) {
      target = real;
      ssh_string_free_char(real);
    }

    // Only saves changes to a file that already exists; remember its mode so
    // the replacement keeps it.
    SftpAttrPtr original{sftp_stat(sftp, target.c_str())};
    if (!original || original->type != SSH_FILEXFER_TYPE_REGULAR) {
      return Status::Error(
          wxString::Format("%s is not an existing regular file", path));
    }
    const mode_t mode = (original->flags & SSH_FILEXFER_ATTR_PERMISSIONS)
                            ? static_cast<mode_t>(original->permissions & 07777)
                            : static_cast<mode_t>(0644);

    // thread_local: saves can run concurrently on different threads.
    thread_local std::mt19937_64 rng{std::random_device{}()};
    const size_t slash = target.rfind('/');
    const std::string dir =
        slash == std::string::npos ? "" : target.substr(0, slash + 1);
    const std::string name =
        slash == std::string::npos ? target : target.substr(slash + 1);
    const std::string tmp =
        dir + "." + name + ".kennel-" + std::to_string(rng() % 1000000000ULL);

    // Private while it is being written; the final mode is applied before the
    // rename so the file never appears at the target with the wrong mode.
    SftpFilePtr file{
        sftp_open(sftp, tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600)};
    if (!file) {
      return SshError(wxString::Format(
                          "Could not create a temporary file next to %s", path),
                      session);
    }

    auto cleanup = [&sftp, &tmp] { sftp_unlink(sftp, tmp.c_str()); };

    size_t offset = 0;
    while (offset < content.size()) {
      const size_t chunk = std::min<size_t>(32 * 1024, content.size() - offset);
      const ssize_t n = sftp_write(file.get(), content.data() + offset, chunk);
      if (n < 0) {
        file.reset();
        cleanup();
        return SshError(wxString::Format("Could not write %s", path), session);
      }
      offset += static_cast<size_t>(n);
    }
    file.reset(); // Close before renaming.

    if (sftp_chmod(sftp, tmp.c_str(), mode) != SSH_OK) {
      cleanup();
      return SshError("Could not preserve the file's permissions", session);
    }
    // Best effort: only root can give a file to someone else, and a failure
    // here must not block the save.
    if (original->flags & SSH_FILEXFER_ATTR_UIDGID) {
      sftp_chown(sftp, tmp.c_str(), original->uid, original->gid);
    }

    // OpenSSH servers rename atomically over an existing file (posix-rename);
    // a server without that extension refuses, and the original stays intact.
    if (sftp_rename(sftp, tmp.c_str(), target.c_str()) != SSH_OK) {
      cleanup();
      return SshError(wxString::Format("Could not replace %s", path), session);
    }
    return Status::Ok();
  });
}

StatusOr<bool> SftpClient::Exists(const wxString &host, const wxString &user,
                                  const wxString &path) {
  return WithConnection<StatusOr<bool>>(
      host, user, [&](Connection &conn) -> StatusOr<bool> {
        sftp_session sftp = conn.sftp.get();
        ssh_session session = conn.ssh.get();
        const wxString &home = conn.home;

        // The session is reused: the error code may still be the one of an
        // earlier call. libssh does not set it when the transport fails.
        sftp->errnum = SSH_FX_OK;
        SftpAttrPtr attrs{sftp_stat(
            sftp, ExpandHome(path, home).ToStdString(wxConvUTF8).c_str())};
        if (attrs) {
          return true;
        }
        if (!conn.IsHealthy()) {
          return SshError(wxString::Format("Could not check %s", path),
                          session);
        }
        // Only "no such file" means no. Anything else (permission denied, a
        // timeout, ...) is an error the caller must not take for an answer.
        const int code = sftp_get_error(sftp);
        if (code == SSH_FX_NO_SUCH_FILE || code == SSH_FX_NO_SUCH_PATH) {
          return false;
        }
        return SshError(
            wxString::Format("Could not check %s (SFTP error %d)", path, code),
            session);
      });
}

Status SftpClient::PutFile(const wxString &host, const wxString &user,
                           const wxString &path, const std::string &content) {
  return WithConnection<Status>(host, user, [&](Connection &conn) -> Status {
    sftp_session sftp = conn.sftp.get();
    ssh_session session = conn.ssh.get();
    const wxString &home = conn.home;
    const std::string target = ExpandHome(path, home).ToStdString(wxConvUTF8);

    // Create the missing parent folders, one level at a time.
    for (size_t slash = target.find('/', 1); slash != std::string::npos;
         slash = target.find('/', slash + 1)) {
      const std::string dir = target.substr(0, slash);
      SftpAttrPtr attrs{sftp_stat(sftp, dir.c_str())};
      if (!attrs && sftp_mkdir(sftp, dir.c_str(), 0755) != SSH_OK) {
        // Somebody may have created it in the meantime; servers do not agree on
        // the error code for that, so look again.
        attrs.reset(sftp_stat(sftp, dir.c_str()));
        if (!attrs) {
          return SshError(wxString::Format("Could not create %s", dir),
                          session);
        }
      }
      if (attrs && attrs->type != SSH_FILEXFER_TYPE_DIRECTORY) {
        return Status::Error(wxString::Format("%s is not a folder", dir));
      }
    }

    SftpFilePtr file{
        sftp_open(sftp, target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644)};
    if (!file) {
      return SshError(wxString::Format("Could not create %s", path), session);
    }
    size_t offset = 0;
    while (offset < content.size()) {
      const size_t chunk = std::min<size_t>(32 * 1024, content.size() - offset);
      const ssize_t n = sftp_write(file.get(), content.data() + offset, chunk);
      if (n < 0) {
        return SshError(wxString::Format("Could not write %s", path), session);
      }
      offset += static_cast<size_t>(n);
    }
    return Status::Ok();
  });
}

Status SftpClient::RemoveTree(const wxString &host, const wxString &user,
                              const wxString &path,
                              const std::vector<wxString> &emptyParents) {
  wxString bare = path;
  while (bare.length() > 1 && bare.EndsWith("/")) {
    bare.RemoveLast();
  }
  if (bare.empty() || bare == "/" || bare == "~" || bare == "$HOME") {
    return Status::Error("Refusing to remove " + path);
  }
  return WithConnection<Status>(host, user, [&](Connection &conn) -> Status {
    sftp_session sftp = conn.sftp.get();
    ssh_session session = conn.ssh.get();
    const wxString &home = conn.home;

    Status result = Status::Ok();
    const std::string target = ExpandHome(path, home).ToStdString(wxConvUTF8);
    SftpAttrPtr attrs{sftp_lstat(sftp, target.c_str())};
    if (attrs && attrs->type == SSH_FILEXFER_TYPE_DIRECTORY) {
      result = RemoveRecursive(sftp, session, target, 0);
    } else if (attrs && sftp_unlink(sftp, target.c_str()) != SSH_OK) {
      // A symbolic link or a file: remove just that.
      result = SshError(wxString::Format("Could not remove %s", path), session);
    }

    // Best effort: sftp_rmdir() fails on a folder that is not empty, which is
    // what we want then.
    for (const wxString &parent : emptyParents) {
      sftp_rmdir(sftp,
                 ExpandHome(parent, home).ToStdString(wxConvUTF8).c_str());
    }
    return result;
  });
}
