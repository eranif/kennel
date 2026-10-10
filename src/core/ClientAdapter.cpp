#include "core/ClientAdapter.h"

#include <wx/arrstr.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>

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

// Whether `chat` is one of the words in `args` (an entry can hold several).
bool HasChat(const std::vector<wxString> &args) {
  for (const wxString &arg : args) {
    for (const wxString &word : wxSplit(arg, ' ')) {
      if (word == "chat") {
        return true;
      }
    }
  }
  return false;
}

} // namespace

ClientKind ClientKindOf(const wxString &executable) {
  const wxString name = wxFileName(executable).GetName().Lower();
  if (name == "claude") {
    return ClientKind::Claude;
  }
  if (name == "codex") {
    return ClientKind::Codex;
  }
  if (name == "kiro-cli" || name == "kiro-cli-chat") {
    return ClientKind::Kiro;
  }
  return ClientKind::Unknown;
}

void ApplyClientDefaults(AgentDef &agent) {
  switch (ClientKindOf(agent.executable)) {
  case ClientKind::Claude:
    agent.resumeArg = "--continue";
    agent.nonInteractiveArg = "-p";
    break;
  case ClientKind::Codex:
    agent.resumeArg = "resume --last";
    agent.nonInteractiveArg = "exec";
    break;
  case ClientKind::Kiro:
    agent.resumeArg = "chat --resume";
    agent.nonInteractiveArg = "chat --no-interactive";
    break;
  case ClientKind::Unknown:
    agent.resumeArg.clear();
    agent.nonInteractiveArg.clear();
    break;
  }
}

wxString DefaultIconFor(const AgentDef &agent) {
  wxString base;
  switch (ClientKindOf(agent.executable)) {
  case ClientKind::Claude:
    base = "claude-code";
    break;
  case ClientKind::Codex:
    base = "codex";
    break;
  case ClientKind::Kiro:
    base = "kiro";
    break;
  case ClientKind::Unknown:
    return "agent.svg";
  }
  if (agent.IsRemote()) {
    return base + "-remote.svg";
  }
  if (agent.IsWSL()) {
    return base + "-wsl.svg";
  }
  return base + ".svg";
}

std::vector<wxString> BuildCommandLine(const AgentDef &agent,
                                       const wxString &workingDir, bool resume,
                                       const wxString &initialPrompt) {
  // The resume argument can hold several words. wxStringTokenize does not
  // honor quotes, so a value that contains spaces is split.
  const std::vector<wxString> resumeArgs =
      wxStringTokenize(agent.resumeArg, " \t", wxTOKEN_STRTOK);

  auto build = [&](bool withResume) {
    std::vector<wxString> args = agent.baseArgs;
    const bool isKiro = ClientKindOf(agent.executable) == ClientKind::Kiro;
    // kiro-cli takes a first message only as an argument of "chat", and a flag
    // like --resume belongs after it.
    if (!initialPrompt.empty() && isKiro) {
      std::vector<wxString> all = args;
      all.insert(all.end(), agent.extraArgs.begin(), agent.extraArgs.end());
      if (withResume) {
        all.insert(all.end(), resumeArgs.begin(), resumeArgs.end());
      }
      if (!HasChat(all)) {
        args.push_back("chat");
      }
    }

    if (withResume) {
      // kiro: do not add a second "chat" when the resume argument repeats it.
      const bool hasChat =
          isKiro && (HasChat(args) || HasChat(agent.extraArgs));
      for (const wxString &arg : resumeArgs) {
        if (!(hasChat && arg == "chat")) {
          args.push_back(arg);
        }
      }
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
    return cmd;
  };

  wxString cmd = build(false);
  if (resume && !agent.resumeArg.empty()) {
    // Resuming fails when there is no earlier session: start a new one then.
    cmd = build(true) + " || " + cmd;
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
