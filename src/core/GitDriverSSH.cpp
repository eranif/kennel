#include "core/GitDriverSSH.h"

#include "core/SshClient.h"

namespace {
// `text` as one word for a POSIX shell.
std::string ShellQuote(const std::string &text) {
  std::string quoted = "'";
  for (const char c : text) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  return quoted + "'";
}

// The folder as the argument of `cd`. A leading `~` or `$HOME` is the home of
// the user on the host: it becomes "$HOME" in double quotes, which the shell
// expands (in single quotes it would not).
std::string CdTarget(const wxString &folder) {
  for (const wxString home : {"~", "$HOME"}) {
    if (folder == home) {
      return "\"$HOME\"";
    }
    if (folder.StartsWith(home + "/")) {
      return "\"$HOME\"/" +
             ShellQuote(folder.Mid(home.length() + 1).ToStdString(wxConvUTF8));
    }
  }
  return ShellQuote(folder.ToStdString(wxConvUTF8));
}
} // namespace

ProcessOutput GitDriverSSH::RunGit(const std::vector<std::string> &args) {
  // One command line for the shell of the host. git must not ask for
  // credentials: nobody can answer.
  std::string command =
      "cd " + CdTarget(GetFolder()) + " && GIT_TERMINAL_PROMPT=0 git";
  for (const auto &arg : args) {
    command += " " + ShellQuote(arg);
  }
  auto result = SshClient::Execute(m_host, m_user, wxString::FromUTF8(command));
  ProcessOutput output;
  if (!result.ok()) {
    output.ok = false;
    output.err = result.status().message().ToStdString(wxConvUTF8);
    return output;
  }
  SshClient::Result &run = result.value();
  output.ok = run.exitCode == 0;
  output.out = std::move(run.out);
  output.err = std::move(run.err);
  // No exit code: say why. A plain exit code other than 0 keeps the output as
  // it is (GitDriver::OnBranchNameOutput() needs an empty stderr to tell a
  // detached HEAD).
  wxString why;
  if (!run.exitSignal.empty()) {
    why = wxString::Format("git was stopped by the signal %s", run.exitSignal);
  } else if (run.exitCode == -1) {
    why = "The host did not send the exit code of git";
  }
  if (!why.empty()) {
    output.err +=
        (output.err.empty() ? "" : "\n") + why.ToStdString(wxConvUTF8);
  }
  return output;
}
