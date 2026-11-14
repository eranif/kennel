#include "core/RunShell.h"

#include "core/Process.hpp"
#include "core/ShellQuote.h"
#include "core/SshClient.h"

#include <vector>

StatusOr<ShellResult> RunShell(const Account &account,
                               const std::string &script,
                               std::chrono::seconds timeout) {
  if (account.IsRemote()) {
    auto reply = SshClient::Execute(
        account.m_name, account.m_user,
        wxString::FromUTF8("sh -c " + ShellQuote(script)), timeout);
    if (!reply.ok()) {
      return reply.status();
    }
    ShellResult result;
    result.exitCode = reply->exitCode;
    result.out = reply->out;
    result.err = reply->err;
    return result;
  }

  std::vector<std::string> argv;
  if (account.IsWSL()) {
    argv = {"wsl.exe", "-d", account.m_name.ToStdString(wxConvUTF8), "--exec"};
  }
  argv.push_back("sh");
  argv.push_back("-c");
  argv.push_back(script);
  // On Windows the program gets one command line: the script has spaces.
  for (std::string &arg : argv) {
    arg = Process::QuoteArgument(arg);
  }
  ShellResult result;
  result.exitCode = Process::RunProcessAndWait(
      argv, [&](const std::string &out, const std::string &err) {
        result.out += out;
        result.err += err;
        return true;
      });
  // -1 is what RunProcessAndWait returns when the program could not be started.
  // Any other number is an exit code, also a negative one: on Windows a program
  // that was ended by an error (an NTSTATUS such as 0xC0000005) has one, and it
  // is for the caller to judge like any other code that is not 0.
  if (result.exitCode == -1) {
    return Status::Error(wxString::Format(_("Could not run a command on %s"),
                                          account.GetDisplayName()));
  }
  return result;
}
