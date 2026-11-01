#include "core/SshSession.h"

namespace ssh_session_detail {

namespace {
constexpr int kConnectTimeoutSecs = 10;

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

} // namespace

Status SshError(const wxString &what, ssh_session session) {
  return Status::Error(wxString::Format(
      "%s: %s", what, wxString::FromUTF8(ssh_get_error(session))));
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

wxString PoolKey(const wxString &host, const wxString &user) {
  return user.empty() ? host : user + "@" + host;
}

} // namespace ssh_session_detail
