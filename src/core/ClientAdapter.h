#pragma once

#include <wx/string.h>

#include "core/Config.h"

#include <vector>

// The CLI tools Kennel supports. Anything else needs code changes here: the
// resume and one-shot arguments are fixed per tool, not set by the user.
enum class ClientKind { Unknown, Claude, Codex, Kiro };

// The tool behind `executable`: the file name decides (a path, a ".exe" or a
// ".cmd" is fine).
ClientKind ClientKindOf(const wxString &executable);
inline bool IsSupportedClient(const wxString &executable) {
  return ClientKindOf(executable) != ClientKind::Unknown;
}

// Sets agent.resumeArg and agent.nonInteractiveArg from the tool. Both are
// empty for an unsupported tool.
void ApplyClientDefaults(AgentDef &agent);

// The shipped icon (a file name in the assets folder) for an agent that has
// none: by tool, and by where it runs (remote, WSL or local). Needs the
// executable, remoteHost and loginShell of `agent`.
wxString DefaultIconFor(const AgentDef &agent);

// Builds the list of commands to send to the terminal to launch a session.
// When resume is true and agent.resumeArg is non-empty, the resume arg is
// appended before agent.extraArgs. The command is then
// "<resume cmd> || <plain cmd>", so a fresh session starts when there is
// nothing to resume. This also starts a new session when the resumed one ends
// with a non-zero exit code.
// A non-empty `initialPrompt` is appended last, as a quoted argument: the agent
// starts interactively and submits it as its first message
// (`kiro-cli chat "<prompt>"`, `claude "<prompt>"`). For kiro-cli, `chat` is
// added before the other arguments if no argument has it. The prompt is
// escaped for a POSIX shell (also over ssh and in WSL). A Windows shell (cmd,
// PowerShell) does not understand that escaping, so keep the prompt to plain
// text there.
std::vector<wxString> BuildCommandLine(const AgentDef &agent,
                                       const wxString &workingDir, bool resume,
                                       const wxString &initialPrompt = {});

// Builds the list of commands to send to the terminal to run a single
// "Prompt" job non-interactively: like BuildCommandLine, but appends
// agent.nonInteractiveArg (instead of resumeArg) and the prompt text as the
// final quoted argument.
std::vector<wxString> BuildJobCommandLine(const AgentDef &agent,
                                          const wxString &workingDir,
                                          const wxString &prompt);
