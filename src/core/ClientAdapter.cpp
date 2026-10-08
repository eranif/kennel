#include "core/ClientAdapter.h"

namespace {

std::vector<wxString> WrapCommand(const AgentDef &agent,
                                  const wxString &workingDir,
                                  const wxString &cmd) {
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
      commands.push_back(wxString::Format(R"(mkdir -p "%s" && cd "%s")",
                                          workingDir, workingDir));
    }
    for (const auto &[name, value] : agent.env) {
      commands.push_back(wxString::Format("export %s=%s", name, value));
    }
  } else if (agent.IsBash()) {
    commands.push_back(wxString::Format(R"(mkdir -p "%s" && cd "%s")",
                                        workingDir, workingDir));
  }

  commands.push_back(cmd);
  return commands;
}

// `text` in double quotes, for a POSIX shell (also over ssh and in WSL): the
// four characters that stay special inside double quotes get a backslash.
wxString QuoteForShell(const wxString &text) {
  wxString quoted = "\"";
  for (const wxUniChar c : text) {
    if (c == '"' || c == '$' || c == '`' || c == '\\') {
      quoted << '\\';
    }
    quoted << c;
  }
  quoted << '"';
  return quoted;
}

} // namespace

std::vector<wxString> BuildCommandLine(const AgentDef &agent,
                                       const wxString &workingDir, bool resume,
                                       const wxString &initialPrompt) {
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
  if (!initialPrompt.empty()) {
    cmd << " " << QuoteForShell(initialPrompt);
  }

  return WrapCommand(agent, workingDir, cmd);
}

std::vector<wxString> BuildJobCommandLine(const AgentDef &agent,
                                          const wxString &workingDir,
                                          const wxString &prompt) {
  std::vector<wxString> args = agent.baseArgs;

  if (!agent.nonInteractiveArg.empty()) {
    args.push_back(agent.nonInteractiveArg);
  }

  for (const wxString &arg : agent.extraArgs) {
    args.push_back(arg);
  }

  wxString cmd = wxString::Format(R"("%s")", agent.executable);
  for (const wxString &arg : args) {
    cmd << " " << arg;
  }
  cmd << " " << wxString::Format(R"("%s")", prompt);

  return WrapCommand(agent, workingDir, cmd);
}
