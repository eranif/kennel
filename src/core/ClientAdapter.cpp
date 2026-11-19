#include "core/ClientAdapter.h"

#include "core/TmuxShim.h"

#include <wx/arrstr.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>

namespace {

std::vector<wxString> WrapCommand(const AgentDef &agent,
                                  const wxString &workingDir,
                                  const wxString &cmd) {
  std::vector<wxString> commands;
  if (!agent.remoteHost.empty()) {
    commands.push_back(SshLoginCommand(agent.remoteHost, agent.remoteUser));
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
  if (name == "opencode") {
    return ClientKind::OpenCode;
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
  case ClientKind::OpenCode:
    agent.resumeArg = "--continue";
    agent.nonInteractiveArg = "run";
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
  case ClientKind::OpenCode:
    base = "opencode";
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

std::vector<wxString>
BuildCommandLine(const AgentDef &agent, const wxString &workingDir, bool resume,
                 const wxString &initialPrompt,
                 const std::optional<tmux::LaunchSpec> &tmuxSpec) {
  // The resume argument can hold several words. wxStringTokenize does not
  // honor quotes, so a value that contains spaces is split.
  const std::vector<wxString> resumeArgs =
      wxStringTokenize(agent.resumeArg, " \t", wxTOKEN_STRTOK);

  // `withShim`: the program runs through tmux-shim (see TmuxShim.h).
  auto build = [&](bool withResume, bool withShim) {
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

    wxString cmd;
    if (withShim) {
      cmd << tmux::ShimPrefix(tmuxshim::ShellPath()) << " ";
    }
    cmd << wxString::Format(R"("%s")", agent.executable);
    for (const wxString &arg : args) {
      cmd << " " << arg;
    }
    if (!initialPrompt.empty()) {
      // opencode takes a bare argument as a project path: the first message
      // needs --prompt.
      if (ClientKindOf(agent.executable) == ClientKind::OpenCode) {
        cmd << " --prompt";
      }
      cmd << " " << QuoteForShell(initialPrompt);
    }
    return cmd;
  };

  auto chain = [&](bool withShim) {
    wxString cmd = build(false, withShim);
    if (resume && !agent.resumeArg.empty()) {
      // Resuming fails when there is no earlier session: start a new one then.
      cmd = build(true, withShim) + " || " + cmd;
    }
    return cmd;
  };

  if (!tmuxSpec) {
    return WrapCommand(agent, workingDir, chain(false));
  }

  // In tmux the program does not start in the shell that Kennel typed into: it
  // gets the environment of the tmux server (the first client's, for a server
  // that is running already), so the variables of the agent are set here. It
  // runs through tmux-shim when that is installed on the machine (the check is
  // made there, when the command runs: a missing shim never stops the
  // session).
  wxString command;
  for (const auto &[name, value] : agent.env) {
    command << "export " << name << "=" << value << "; ";
  }
  command << "if [ -x \"" << tmuxshim::ShellPath() << "\" ]; then "
          << chain(true) << "; else " << chain(false) << "; fi";

  tmux::LaunchSpec spec = *tmuxSpec;
  spec.command = command;
  // No folder: the shell is in it already (the `cd` of WrapCommand, the folder
  // of the terminal, or `--cd` of wsl.exe).
  return WrapCommand(agent, workingDir, tmux::AttachOrCreate(spec));
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

wxString SshLoginCommand(const wxString &host, const wxString &user) {
  wxString command = wxString::Format("ssh -o ServerAliveInterval=10 %s",
                                      user.empty() ? host : user + "@" + host);
#ifndef __WXMSW__
  command << R"(; printf '\033[!p\033[?2004l')";
#endif
  return command;
}

wxString ChangeDirCommand(const wxString &dir) {
  wxString rest = dir;
  // Only a leading $HOME is left for the shell to expand; a tilde is not
  // expanded inside quotes.
  wxString head;
  if (rest == "~") {
    head = "$HOME";
    rest.clear();
  } else if (rest.StartsWith("~/")) {
    head = "$HOME";
    rest = rest.Mid(1);
  } else if (rest == "$HOME" || rest.StartsWith("$HOME/")) {
    head = "$HOME";
    rest = rest.Mid(5);
  }
  // The rest is a folder name: nothing in it is special to the shell, not even
  // `$` (a name such as `a$(cmd)` must not run, nor `$VAR` change the folder).
  wxString quoted = "\"" + head;
  for (const wxUniChar c : rest) {
    if (c == '"' || c == '\\' || c == '`' || c == '$') {
      quoted << '\\';
    }
    quoted << c;
  }
  quoted << '"';
  return "cd " + quoted;
}
