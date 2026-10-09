#include "core/SftpClient.h"

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include <fcntl.h>

#include <algorithm>

#include <memory>
#include <random>

namespace {

struct SshSessionDeleter {
  void operator()(ssh_session s) const {
    if (ssh_is_connected(s)) {
      ssh_disconnect(s);
    }
    ssh_free(s);
  }
};
struct SftpSessionDeleter {
  void operator()(sftp_session s) const { sftp_free(s); }
};
struct SftpFileDeleter {
  void operator()(sftp_file f) const { sftp_close(f); }
};
struct SftpAttrDeleter {
  void operator()(sftp_attributes a) const { sftp_attributes_free(a); }
};

using SshSessionPtr = std::unique_ptr<ssh_session_struct, SshSessionDeleter>;
using SftpSessionPtr = std::unique_ptr<sftp_session_struct, SftpSessionDeleter>;
using SftpFilePtr = std::unique_ptr<sftp_file_struct, SftpFileDeleter>;
using SftpAttrPtr = std::unique_ptr<sftp_attributes_struct, SftpAttrDeleter>;

constexpr int kConnectTimeoutSecs = 10;

Status SshError(const wxString &what, ssh_session session) {
  return Status::Error(wxString::Format(
      "%s: %s", what, wxString::FromUTF8(ssh_get_error(session))));
}

// Verifies the server against ~/.ssh/known_hosts. A host that was never seen is
// trusted and recorded on first use (OpenSSH's `accept-new`); a host whose key
// changed, or is listed under a different key type, is refused.
Status VerifyServer(ssh_session session) {
  switch (ssh_session_is_known_server(session)) {
  case SSH_KNOWN_HOSTS_OK:
    return Status::Ok();
  case SSH_KNOWN_HOSTS_NOT_FOUND:
  case SSH_KNOWN_HOSTS_UNKNOWN:
    // Best effort: failing to record the key must not fail the connection.
    // (No logging here: this runs on worker threads and Logger is not
    // thread-safe.)
    ssh_session_update_known_hosts(session);
    return Status::Ok();
  case SSH_KNOWN_HOSTS_CHANGED:
    return Status::Error("The host key has changed (possible "
                         "man-in-the-middle attack); refusing to connect");
  case SSH_KNOWN_HOSTS_OTHER:
    return Status::Error("The host key type differs from the one recorded "
                         "in known_hosts; refusing to connect");
  default:
    return SshError("Host key verification failed", session);
  }
}

StatusOr<SshSessionPtr> Connect(const wxString &host, const wxString &user) {
  SshSessionPtr session{ssh_new()};
  if (!session) {
    return Status::Error("Could not allocate an SSH session");
  }

  const std::string hostUtf8 = host.ToStdString(wxConvUTF8);
  const std::string userUtf8 = user.ToStdString(wxConvUTF8);
  long timeout = kConnectTimeoutSecs;
  ssh_options_set(session.get(), SSH_OPTIONS_HOST, hostUtf8.c_str());
  ssh_options_set(session.get(), SSH_OPTIONS_TIMEOUT, &timeout);
  if (!userUtf8.empty()) {
    ssh_options_set(session.get(), SSH_OPTIONS_USER, userUtf8.c_str());
  }
  // Apply ~/.ssh/config (HostName, Port, IdentityFile, User, ...) for this
  // host. Explicit options set above win only for fields the config omits, so
  // re-apply the user afterwards.
  ssh_options_parse_config(session.get(), nullptr);
  if (!userUtf8.empty()) {
    ssh_options_set(session.get(), SSH_OPTIONS_USER, userUtf8.c_str());
  }

  if (ssh_connect(session.get()) != SSH_OK) {
    return SshError("Could not connect", session.get());
  }
  if (Status st = VerifyServer(session.get()); !st.ok()) {
    return st;
  }
  if (ssh_userauth_publickey_auto(session.get(), nullptr, nullptr) !=
      SSH_AUTH_SUCCESS) {
    return SshError("Authentication failed (key-based login is required)",
                    session.get());
  }
  return session;
}

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
  auto session = Connect(host, user);
  if (!session.ok()) {
    return session.status();
  }

  SftpSessionPtr sftp{sftp_new(session.value().get())};
  if (!sftp || sftp_init(sftp.get()) != SSH_OK) {
    return SshError("Could not start the SFTP subsystem",
                    session.value().get());
  }

  wxString home;
  if (char *real = sftp_canonicalize_path(sftp.get(), ".")) {
    home = wxString::FromUTF8(real);
    ssh_string_free_char(real);
  }

  std::vector<wxString> candidates;
  if (path.StartsWith("/")) {
    candidates.push_back(path);
  } else if (path == "~" || path.StartsWith("~/")) {
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
    if (!IsRegularFile(sftp.get(), candidate, &size)) {
      continue;
    }
    if (size > maxBytes) {
      return Status::Error(
          wxString::Format("%s is too large to open (%llu bytes)", candidate,
                           static_cast<unsigned long long>(size)));
    }

    SftpFilePtr file{sftp_open(
        sftp.get(), candidate.ToStdString(wxConvUTF8).c_str(), O_RDONLY, 0)};
    if (!file) {
      return SshError(wxString::Format("Could not open %s", candidate),
                      session.value().get());
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
                        session.value().get());
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
}

Status SftpClient::WriteFile(const wxString &host, const wxString &user,
                             const wxString &path, const std::string &content) {
  auto session = Connect(host, user);
  if (!session.ok()) {
    return session.status();
  }

  SftpSessionPtr sftp{sftp_new(session.value().get())};
  if (!sftp || sftp_init(sftp.get()) != SSH_OK) {
    return SshError("Could not start the SFTP subsystem",
                    session.value().get());
  }

  // Replace the real file, not a symlink pointing at it.
  std::string target = path.ToStdString(wxConvUTF8);
  if (char *real = sftp_canonicalize_path(sftp.get(), target.c_str())) {
    target = real;
    ssh_string_free_char(real);
  }

  // Only saves changes to a file that already exists; remember its mode so the
  // replacement keeps it.
  SftpAttrPtr original{sftp_stat(sftp.get(), target.c_str())};
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
      sftp_open(sftp.get(), tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600)};
  if (!file) {
    return SshError(
        wxString::Format("Could not create a temporary file next to %s", path),
        session.value().get());
  }

  auto cleanup = [&sftp, &tmp] { sftp_unlink(sftp.get(), tmp.c_str()); };

  size_t offset = 0;
  while (offset < content.size()) {
    const size_t chunk = std::min<size_t>(32 * 1024, content.size() - offset);
    const ssize_t n = sftp_write(file.get(), content.data() + offset, chunk);
    if (n < 0) {
      file.reset();
      cleanup();
      return SshError(wxString::Format("Could not write %s", path),
                      session.value().get());
    }
    offset += static_cast<size_t>(n);
  }
  file.reset(); // Close before renaming.

  if (sftp_chmod(sftp.get(), tmp.c_str(), mode) != SSH_OK) {
    cleanup();
    return SshError("Could not preserve the file's permissions",
                    session.value().get());
  }
  // Best effort: only root can give a file to someone else, and a failure
  // here must not block the save.
  if (original->flags & SSH_FILEXFER_ATTR_UIDGID) {
    sftp_chown(sftp.get(), tmp.c_str(), original->uid, original->gid);
  }

  // OpenSSH servers rename atomically over an existing file (posix-rename);
  // a server without that extension refuses, and the original stays intact.
  if (sftp_rename(sftp.get(), tmp.c_str(), target.c_str()) != SSH_OK) {
    cleanup();
    return SshError(wxString::Format("Could not replace %s", path),
                    session.value().get());
  }
  return Status::Ok();
}

StatusOr<bool> SftpClient::Exists(const wxString &host, const wxString &user,
                                  const wxString &path) {
  auto session = Connect(host, user);
  if (!session.ok()) {
    return session.status();
  }

  SftpSessionPtr sftp{sftp_new(session.value().get())};
  if (!sftp || sftp_init(sftp.get()) != SSH_OK) {
    return SshError("Could not start the SFTP subsystem",
                    session.value().get());
  }

  wxString home;
  if (char *real = sftp_canonicalize_path(sftp.get(), ".")) {
    home = wxString::FromUTF8(real);
    ssh_string_free_char(real);
  }

  SftpAttrPtr attrs{sftp_stat(
      sftp.get(), ExpandHome(path, home).ToStdString(wxConvUTF8).c_str())};
  if (attrs) {
    return true;
  }
  // Only "no such file" means no. Anything else (permission denied, ...) is an
  // error the caller must not take for an answer.
  const int code = sftp_get_error(sftp.get());
  if (code == SSH_FX_NO_SUCH_FILE || code == SSH_FX_NO_SUCH_PATH) {
    return false;
  }
  return SshError(
      wxString::Format("Could not check %s (SFTP error %d)", path, code),
      session.value().get());
}

Status SftpClient::PutFile(const wxString &host, const wxString &user,
                           const wxString &path, const std::string &content) {
  auto session = Connect(host, user);
  if (!session.ok()) {
    return session.status();
  }

  SftpSessionPtr sftp{sftp_new(session.value().get())};
  if (!sftp || sftp_init(sftp.get()) != SSH_OK) {
    return SshError("Could not start the SFTP subsystem",
                    session.value().get());
  }

  wxString home;
  if (char *real = sftp_canonicalize_path(sftp.get(), ".")) {
    home = wxString::FromUTF8(real);
    ssh_string_free_char(real);
  }
  const std::string target = ExpandHome(path, home).ToStdString(wxConvUTF8);

  // Create the missing parent folders, one level at a time.
  for (size_t slash = target.find('/', 1); slash != std::string::npos;
       slash = target.find('/', slash + 1)) {
    const std::string dir = target.substr(0, slash);
    SftpAttrPtr attrs{sftp_stat(sftp.get(), dir.c_str())};
    if (!attrs && sftp_mkdir(sftp.get(), dir.c_str(), 0755) != SSH_OK) {
      // Somebody may have created it in the meantime; servers do not agree on
      // the error code for that, so look again.
      attrs.reset(sftp_stat(sftp.get(), dir.c_str()));
      if (!attrs) {
        return SshError(wxString::Format("Could not create %s", dir),
                        session.value().get());
      }
    }
    if (attrs && attrs->type != SSH_FILEXFER_TYPE_DIRECTORY) {
      return Status::Error(wxString::Format("%s is not a folder", dir));
    }
  }

  SftpFilePtr file{sftp_open(sftp.get(), target.c_str(),
                             O_WRONLY | O_CREAT | O_TRUNC, 0644)};
  if (!file) {
    return SshError(wxString::Format("Could not create %s", path),
                    session.value().get());
  }
  size_t offset = 0;
  while (offset < content.size()) {
    const size_t chunk = std::min<size_t>(32 * 1024, content.size() - offset);
    const ssize_t n = sftp_write(file.get(), content.data() + offset, chunk);
    if (n < 0) {
      return SshError(wxString::Format("Could not write %s", path),
                      session.value().get());
    }
    offset += static_cast<size_t>(n);
  }
  return Status::Ok();
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
  auto session = Connect(host, user);
  if (!session.ok()) {
    return session.status();
  }

  SftpSessionPtr sftp{sftp_new(session.value().get())};
  if (!sftp || sftp_init(sftp.get()) != SSH_OK) {
    return SshError("Could not start the SFTP subsystem",
                    session.value().get());
  }

  wxString home;
  if (char *real = sftp_canonicalize_path(sftp.get(), ".")) {
    home = wxString::FromUTF8(real);
    ssh_string_free_char(real);
  }

  Status result = Status::Ok();
  const std::string target = ExpandHome(path, home).ToStdString(wxConvUTF8);
  SftpAttrPtr attrs{sftp_lstat(sftp.get(), target.c_str())};
  if (attrs && attrs->type == SSH_FILEXFER_TYPE_DIRECTORY) {
    result = RemoveRecursive(sftp.get(), session.value().get(), target, 0);
  } else if (attrs && sftp_unlink(sftp.get(), target.c_str()) != SSH_OK) {
    // A symbolic link or a file: remove just that.
    result = SshError(wxString::Format("Could not remove %s", path),
                      session.value().get());
  }

  // Best effort: sftp_rmdir() fails on a folder that is not empty, which is
  // what we want then.
  for (const wxString &parent : emptyParents) {
    sftp_rmdir(sftp.get(),
               ExpandHome(parent, home).ToStdString(wxConvUTF8).c_str());
  }
  return result;
}
