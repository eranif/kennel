#include "core/ClientAdapter.h"

std::vector<wxString> BuildCommandLine(const AgentDef &agent,
                                       const wxString &workingDir,
                                       bool resume) {
  std::vector<wxString> args = agent.baseArgs;

  if (resume && !agent.resumeArg.empty()) {
    args.push_back(agent.resumeArg);
  }

  for (const wxString &arg : agent.extraArgs) {
    args.push_back(arg);
  }

  wxString cmd = wxString::Format(R"("%s")", agent.executable);
  for (const wxString &arg : args) {
    cmd << " " << arg;
  }

  std::vector<wxString> commands;
  if (!agent.remoteHost.empty()) {
    wxString loginCommand;
    if (agent.remoteUser.empty()) {
      loginCommand = wxString::Format("ssh -o ServerAliveInterval=10 %s",
                                      agent.remoteHost);
    } else {
      loginCommand = wxString::Format("ssh -o ServerAliveInterval=10 %s@%s",
                                      agent.remoteUser, agent.remoteHost);
    }
    commands.push_back(loginCommand);
    if (!workingDir.empty()) {
      commands.push_back(
          wxString::Format("mkdir -p %s && cd %s", workingDir, workingDir));
    }
    for (const auto &[name, value] : agent.env) {
      commands.push_back(wxString::Format("export %s=%s", name, value));
    }
  }

  commands.push_back(cmd);
  return commands;
}
