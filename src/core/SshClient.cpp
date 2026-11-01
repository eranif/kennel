#include "core/SshClient.h"

#include "core/SshSession.h"

#include <libssh/libssh.h>

#include <memory>
#include <utility>

namespace {

using ssh_session_detail::Connect;
using ssh_session_detail::ConnectionPool;
using ssh_session_detail::PoolKey;
using ssh_session_detail::SshError;
using ssh_session_detail::SshSessionPtr;

struct SshChannelDeleter {
  void operator()(ssh_channel c) const {
    if (ssh_channel_is_open(c)) {
      ssh_channel_close(c);
    }
    ssh_channel_free(c);
  }
};
using SshChannelPtr = std::unique_ptr<ssh_channel_struct, SshChannelDeleter>;

// A command that writes more is stopped: the output is kept in memory.
constexpr size_t kMaxOutputBytes = 256 * 1024 * 1024;
// How long one read waits for data before the timeout is checked again.
constexpr int kReadSliceMs = 100;

// An open, logged-in SSH session.
struct Connection {
  SshSessionPtr ssh;
  // When it went back to the pool.
  std::chrono::steady_clock::time_point lastUsed;

  bool IsHealthy() const { return ssh && ssh_is_connected(ssh.get()); }
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
  return conn;
}

// Runs `command` (UTF-8) on a new channel of `conn`. `*started` is set just
// before the command is sent: from then on, a failure must not run it again,
// even when the host did not answer (it may have started the command).
StatusOr<SshClient::Result> Run(Connection &conn, const std::string &command,
                                std::chrono::seconds timeout, bool *started) {
  ssh_session session = conn.ssh.get();
  SshChannelPtr channel{ssh_channel_new(session)};
  if (!channel) {
    return SshError("Could not create a channel", session);
  }
  if (ssh_channel_open_session(channel.get()) != SSH_OK) {
    return SshError("Could not open a channel", session);
  }
  *started = true;
  if (ssh_channel_request_exec(channel.get(), command.c_str()) != SSH_OK) {
    return SshError("Could not run the command", session);
  }

  SshClient::Result result;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  // The limits are checked after each chunk, so that a command that never
  // stops writing (to either stream) is stopped too.
  auto checkLimits = [&]() -> Status {
    if (result.out.size() + result.err.size() > kMaxOutputBytes) {
      return Status::Error("The command wrote too much output");
    }
    if (std::chrono::steady_clock::now() > deadline) {
      return Status::Error(
          wxString::Format("The command did not finish in %lld seconds",
                           static_cast<long long>(timeout.count())));
    }
    return Status::Ok();
  };
  char buffer[16 * 1024];
  // Both streams are read in turns, so that a command that writes a lot to
  // one of them never waits for the other one to be read.
  while (!ssh_channel_is_eof(channel.get())) {
    const int n = ssh_channel_read_timeout(channel.get(), buffer,
                                           sizeof(buffer), 0, kReadSliceMs);
    // Only SSH_ERROR is an error. The other negative values are no data:
    // SSH_AGAIN (nothing came in the time slice) and SSH_EOF (the stream is
    // closed; the loop ends once both are).
    if (n == SSH_ERROR) {
      return SshError("Could not read the output", session);
    }
    if (n > 0) {
      result.out.append(buffer, n);
    }
    if (Status st = checkLimits(); !st.ok()) {
      return st;
    }
    int m = 0;
    while ((m = ssh_channel_read_nonblocking(channel.get(), buffer,
                                             sizeof(buffer), 1)) > 0) {
      result.err.append(buffer, m);
      if (Status st = checkLimits(); !st.ok()) {
        return st;
      }
    }
    if (m == SSH_ERROR) {
      return SshError("Could not read the output", session);
    }
  }

  // Waits for the exit state when it comes after the output. SSH_AGAIN: the
  // host closed the channel without one; exitCode stays -1.
  uint32_t exitCode = 0;
  char *signal = nullptr;
  int coreDumped = 0;
  const int rc = ssh_channel_get_exit_state(channel.get(), &exitCode, &signal,
                                            &coreDumped);
  if (rc == SSH_ERROR) {
    return SshError("Could not get the exit code", session);
  }
  if (rc == SSH_OK) {
    if (signal != nullptr) {
      // A command that was stopped by a signal has no exit code.
      result.exitSignal = wxString::FromUTF8(signal);
      ssh_string_free_char(signal);
    } else {
      result.exitCode = static_cast<int>(exitCode);
    }
  }
  return result;
}

} // namespace

StatusOr<SshClient::Result> SshClient::Execute(const wxString &host,
                                               const wxString &user,
                                               const wxString &command,
                                               std::chrono::seconds timeout) {
  const wxString key = PoolKey(host, user);
  const std::string commandUtf8 = command.ToStdString(wxConvUTF8);
  if (ConnectionPtr pooled = Pool().Take(key)) {
    bool started = false;
    auto result = Run(*pooled, commandUtf8, timeout, &started);
    if (result.ok()) {
      Pool().Return(key, std::move(pooled));
      return result;
    }
    // A connection from the pool that failed before the command was sent is
    // worth one more try: the host may have closed it while it was idle.
    if (started) {
      return result;
    }
  }
  auto fresh = NewConnection(host, user);
  if (!fresh.ok()) {
    return fresh.status();
  }
  bool started = false;
  auto result = Run(*fresh.value(), commandUtf8, timeout, &started);
  if (result.ok()) {
    Pool().Return(key, std::move(fresh.value()));
  }
  return result;
}
