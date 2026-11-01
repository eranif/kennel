#pragma once

// The libssh code that SftpClient and SshClient share. Internal: include it
// only in .cpp files of src/core, never in a public header.

#include "core/Status.h"

#include <libssh/libssh.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <wx/string.h>

namespace ssh_session_detail {

struct SshSessionDeleter {
  void operator()(ssh_session s) const {
    if (ssh_is_connected(s)) {
      ssh_disconnect(s);
    }
    ssh_free(s);
  }
};
using SshSessionPtr = std::unique_ptr<ssh_session_struct, SshSessionDeleter>;

// `what`, then the last libssh error of `session`.
Status SshError(const wxString &what, ssh_session session);

// Connects to `host` (as `user`, when non-empty), checks the host key against
// ~/.ssh/known_hosts and logs in with a key (ssh-agent, then the default keys
// in ~/.ssh). `host` may be an ~/.ssh/config alias; the config is honoured.
StatusOr<SshSessionPtr> Connect(const wxString &host, const wxString &user);

// The key of `host` and `user` in a ConnectionPool: a connection is for one
// user, and two users of one host must not throw out each other's connection.
wxString PoolKey(const wxString &host, const wxString &user);

// Open connections that wait for the next call, one for each key (see
// PoolKey()). A call takes the connection out while it uses it (a libssh
// session is not for two threads at a time), so the mutex is never held while
// talking to a host. `Conn` has a member
// `std::chrono::steady_clock::time_point lastUsed` and a method
// `bool IsHealthy() const`.
template <typename Conn> class ConnectionPool {
public:
  using Ptr = std::unique_ptr<Conn>;

  // A connection that was idle longer is not reused: after a long time the
  // host or the network has often closed it without telling.
  static constexpr auto kMaxIdle = std::chrono::minutes(5);

  // The idle connection of `key` when it is healthy and was not idle too long,
  // else nullptr.
  Ptr Take(const wxString &key) {
    Ptr conn;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (auto it = m_idle.find(key); it != m_idle.end()) {
        conn = std::move(it->second);
        m_idle.erase(it);
      }
    }
    // Closed or idle too long: it is freed here, out of the lock.
    if (conn && conn->IsHealthy() &&
        std::chrono::steady_clock::now() - conn->lastUsed < kMaxIdle) {
      return conn;
    }
    return nullptr;
  }

  // Puts `conn` back for the next call, if it is still healthy.
  void Return(const wxString &key, Ptr conn) {
    if (!conn->IsHealthy()) {
      return;
    }
    conn->lastUsed = std::chrono::steady_clock::now();
    Ptr replaced; // Freed after the lock is released
    std::lock_guard<std::mutex> lock(m_mutex);
    // Two calls ran at the same time: the pool keeps one connection.
    replaced = std::exchange(m_idle[key], std::move(conn));
  }

private:
  std::mutex m_mutex;
  std::unordered_map<wxString, Ptr> m_idle;
};

} // namespace ssh_session_detail
