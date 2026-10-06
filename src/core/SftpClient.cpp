#include "core/SftpClient.h"

#include "core/Logger.h"

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include <fcntl.h>

#include <memory>

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
  return Status::Error(
      wxString::Format("%s: %s", what, wxString::FromUTF8(ssh_get_error(session))));
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
    if (ssh_session_update_known_hosts(session) != SSH_OK) {
      KLOG_WARN() << "Could not record host key: " << ssh_get_error(session);
    }
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

} // namespace

StatusOr<SftpClient::RemoteFile>
SftpClient::ReadFile(const wxString &host, const wxString &user,
                     const wxString &path,
                     const std::vector<wxString> &searchDirs,
                     size_t maxBytes) {
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
      return Status::Error(wxString::Format(
          "%s is too large to open (%llu bytes)", candidate,
          static_cast<unsigned long long>(size)));
    }

    SftpFilePtr file{sftp_open(sftp.get(),
                               candidate.ToStdString(wxConvUTF8).c_str(),
                               O_RDONLY, 0)};
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

  return Status::Error(wxString::Format("File not found on %s: %s", host, path));
}
