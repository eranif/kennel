#include "core/TmuxClient.h"

#include "core/Logger.h"
#include "core/RunShell.h"

#include <chrono>

namespace {

// How long one question to tmux may take, on a host that answers.
constexpr std::chrono::seconds kTimeout{30};

// Where tmux can be besides the PATH of a shell that reads no profile (the case
// over ssh, and for `sh -c`): where a package manager puts it (`brew`,
// Homebrew on Linux, `pip --user` style folders), so that the tmux that the
// shell of the user finds when the command is typed is found here too.
constexpr const char *kPathSetup =
    "PATH=\"$PATH:/usr/local/bin:/opt/homebrew/bin:"
    "/home/linuxbrew/.linuxbrew/bin:$HOME/.local/bin:$HOME/bin\"; export "
    "PATH; ";
// What a shell exits with for a program that is not there.
constexpr int kCommandNotFound = 127;

wxString Trimmed(const std::string &text) {
  wxString message = wxString::FromUTF8(text);
  message.Trim().Trim(false);
  return message;
}

// What went wrong when tmux did not answer as expected.
Status Failure(const wxString &what, const ShellResult &answer) {
  const wxString detail = Trimmed(answer.err);
  return Status::Error(
      detail.empty()
          ? wxString::Format("%s (exit code %d)", what, answer.exitCode)
          : wxString::Format("%s: %s", what, detail));
}

// Runs `command` (for a POSIX shell) on the machine of `account`.
StatusOr<ShellResult> Run(const Account &account, const wxString &command) {
#ifdef __WXMSW__
  if (!account.IsWSL() && !account.IsRemote()) {
    return Status::Error(
        _("tmux is not available for the Windows shells of this computer"));
  }
#endif
  auto answer =
      RunShell(account, kPathSetup + command.ToStdString(wxConvUTF8), kTimeout);
  if (answer.ok() && account.IsRemote() && answer->exitCode < 0) {
    // The host sent no exit code: the command was ended by a signal.
    return Status::Error(wxString::Format("tmux on %s did not finish",
                                          account.GetDisplayName()));
  }
  return answer;
}

} // namespace

StatusOr<TmuxProbe> TmuxClient::Probe(const Account &account) {
  auto answer = Run(account, tmux::VersionProbe());
  if (!answer.ok()) {
    return answer.status();
  }
  TmuxProbe probe;
  if (answer->exitCode == kCommandNotFound) {
    return probe; // Not installed
  }
  if (answer->exitCode != 0) {
    return Failure(_("Could not ask tmux for its version"), *answer);
  }
  probe.installed = true;
  probe.banner = Trimmed(answer->out);
  probe.version = tmux::ParseVersion(probe.banner);
  return probe;
}

StatusOr<bool> TmuxClient::HasSession(const Account &account,
                                      tmux::Server server,
                                      const wxString &name) {
  auto answer = Run(account, tmux::HasSession(server, name));
  if (!answer.ok()) {
    return answer.status();
  }
  switch (answer->exitCode) {
  case 0:
    return true;
  case 1:
    return false; // No such session, or no tmux server is running
  case kCommandNotFound:
    return Status::Error(_("tmux is not installed"));
  default:
    return Failure(_("Could not ask tmux for a session"), *answer);
  }
}

StatusOr<std::vector<tmux::SessionInfo>>
TmuxClient::ListSessions(const Account &account, tmux::Server server) {
  auto answer = Run(account, tmux::ListSessions(server));
  if (!answer.ok()) {
    return answer.status();
  }
  if (answer->exitCode == 0) {
    return tmux::ParseSessions(wxString::FromUTF8(answer->out));
  }
  if (answer->exitCode == kCommandNotFound) {
    return Status::Error(_("tmux is not installed"));
  }
  // tmux exits with 1 when its server is not running: it has no sessions.
  const wxString err = Trimmed(answer->err).Lower();
  if (answer->exitCode == 1 && (err.Contains("no server running") ||
                                err.Contains("error connecting to"))) {
    return std::vector<tmux::SessionInfo>{};
  }
  return Failure(_("Could not list the tmux sessions"), *answer);
}

Status TmuxClient::KillSession(const Account &account, tmux::Server server,
                               const wxString &name) {
  // A session that is not there is as good as killed.
  const wxString command = tmux::HasSession(server, name) +
                           " >/dev/null 2>&1 || exit 0; " +
                           tmux::KillSession(server, name);
  auto answer = Run(account, command);
  if (!answer.ok()) {
    return answer.status();
  }
  if (answer->exitCode != 0) {
    return Failure(_("Could not kill the tmux session"), *answer);
  }
  KLOG_INFO() << "Killed the tmux session " << name << " on "
              << account.GetDisplayName();
  return Status::Ok();
}

bool TmuxClient::IsAvailableFor(const Account &account) {
#ifdef __WXMSW__
  return !account.IsLocalHost();
#else
  (void)account;
  return true;
#endif
}
