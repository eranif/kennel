#pragma once

#include "core/Account.h"
#include "core/Status.h"

#include <chrono>
#include <string>

// What a shell command printed and how it ended.
struct ShellResult {
  int exitCode{-1};
  std::string out;
  std::string err;
};

// Runs `script` with `sh -c` on the machine of `account`: over ssh for a remote
// host, with `wsl.exe` for a WSL distribution, directly for this machine. An
// error status only when it could not run at all (no ssh connection, no
// wsl.exe); a script that exits with a code other than 0 is a result, for the
// caller to judge. Blocking: call it from a worker thread. GUI-free.
//
// Over ssh, an exit code of -1 means the host did not send one (the command was
// ended by a signal). `timeout` is for a remote host only: a program started
// here is waited for.
//
// This machine on Windows has no `sh`: the caller must not ask for it there.
StatusOr<ShellResult> RunShell(const Account &account,
                               const std::string &script,
                               std::chrono::seconds timeout);
