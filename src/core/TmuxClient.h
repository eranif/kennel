#pragma once

#include "core/Account.h"
#include "core/Status.h"
#include "core/TmuxCommands.h"

#include <optional>
#include <vector>
#include <wx/string.h>

// What Probe() found.
struct TmuxProbe {
  // tmux runs on the machine.
  bool installed{false};
  // Its version, when it says one.
  std::optional<tmux::Version> version;
  // What `tmux -V` printed ("tmux 3.4").
  wxString banner;

  // Installed and at least tmux::kMinVersion.
  bool IsSupported() const {
    return installed && version.has_value() &&
           version->IsAtLeast(tmux::kMinVersion.major, tmux::kMinVersion.minor);
  }
};

// Asks the tmux of a machine, with the commands of tmux::*: a remote host
// (SshClient), a WSL distribution (`wsl.exe`), or this machine when it is Linux
// or macOS. This machine on Windows has no tmux for its shells: every call
// says so.
//
// Blocking (ssh, a process): call it from a worker thread, never from the UI
// thread. A call that returns an error could not ask (the host can not be
// reached, tmux is not there, an unexpected answer): nothing is known about the
// session then, and none must be created or killed because of it. An answer of
// "no" (HasSession() false) is a StatusOr that holds false.
//
// `tmux` is looked for on the PATH of the machine plus the folders where a
// package manager puts it (`/usr/local/bin`, `/opt/homebrew/bin`), so that a
// host reached without a login shell (the case over ssh) finds the tmux that
// `brew` installed. GUI-free.
class TmuxClient {
public:
  // Whether a session of `account` can run in tmux at all: a remote host, a WSL
  // distribution, or this machine unless it runs Windows (no tmux for its
  // shells). It does not ask the machine (Probe() does).
  static bool IsAvailableFor(const Account &account);

  // Whether tmux is there, and which version.
  static StatusOr<TmuxProbe> Probe(const Account &account);

  // Whether the tmux session `name` of `server` exists. A tmux server that is
  // not running has no sessions: false.
  static StatusOr<bool> HasSession(const Account &account, tmux::Server server,
                                   const wxString &name);

  // The sessions of `server` (none when its tmux server is not running).
  static StatusOr<std::vector<tmux::SessionInfo>>
  ListSessions(const Account &account, tmux::Server server);

  // Ends the session and the program in it. A session that is not there is not
  // an error: the goal is that it is gone.
  static Status KillSession(const Account &account, tmux::Server server,
                            const wxString &name);
};
