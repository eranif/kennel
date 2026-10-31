#include "core/GitDriverSSH.h"

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
} // namespace

ProcessOutput GitDriverSSH::RunGit(const std::vector<std::string> &args) {
  const wxString target = m_user.empty() ? m_host : m_user + "@" + m_host;
  // One command line for the shell of the host. git must not ask for
  // credentials: nobody can answer.
  std::string command = "cd " +
                        ShellQuote(GetFolder().ToStdString(wxConvUTF8)) +
                        " && GIT_TERMINAL_PROMPT=0 git";
  for (const auto &arg : args) {
    command += " " + ShellQuote(arg);
  }
  // "--": a host name is never an option.
  return RunProcess({"ssh", "-T", "-o", "BatchMode=yes", "-o",
                     "ConnectTimeout=10", "--", target.ToStdString(wxConvUTF8),
                     command});
}
