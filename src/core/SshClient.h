#pragma once

#include "core/Status.h"

#include <chrono>
#include <string>
#include <wx/string.h>

// Runs commands on a remote host over SSH (libssh), like `ssh host command`.
// Blocking: call it from a worker thread, never from the UI thread. GUI-free;
// libssh stays out of this header.
//
// The connections are kept open in a pool, one for each host and user, and
// reused by the next call for them; each command gets its own channel. A call
// never shares a connection with another call that runs at the same time. Only
// a connection whose call worked goes back to the pool, and one that was idle
// for more than 5 minutes is not reused. When a host closed a connection while
// it was idle, the call is made again on a new connection, but only when the
// command was not sent yet: a command never runs twice.
class SshClient {
public:
  struct Result {
    // -1 when there is none: the command was stopped by a signal, or the host
    // did not send the exit code.
    int exitCode{-1};
    wxString exitSignal; // The signal that stopped the command, e.g. "KILL"
    std::string out;     // Raw bytes of stdout
    std::string err;     // Raw bytes of stderr
  };

  // Connects to `host` (as `user`, when non-empty) and runs `command` with the
  // shell of the user on the host. There is no terminal (like `ssh -T`), so
  // nothing can ask a question. `host` may be an ~/.ssh/config alias; the
  // config is honoured.
  //
  // Authentication is key-based only (ssh-agent, then the default keys in
  // ~/.ssh), the same requirement as Kennel's other remote features.
  //
  // A command that exits with a code other than 0 is not an error: the error
  // is for "the command could not run, or did not finish in `timeout`". The
  // command is sent as UTF-8.
  static StatusOr<Result>
  Execute(const wxString &host, const wxString &user, const wxString &command,
          std::chrono::seconds timeout = std::chrono::minutes(2));
};
