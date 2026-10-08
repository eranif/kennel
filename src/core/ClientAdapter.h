#pragma once

#include <wx/string.h>

#include "core/Config.h"

#include <vector>

// Builds the list of commands to send to the terminal to launch a session.
// When resume is true and agent.resumeArg is non-empty, the resume arg is
// appended before agent.extraArgs. A non-empty `initialPrompt` is appended
// last, as a quoted argument: the agent starts interactively and submits it as
// its first message (`kiro-cli chat "<prompt>"`, `claude "<prompt>"`). The
// prompt is escaped for a POSIX shell (also over ssh and in WSL). A Windows
// shell (cmd, PowerShell) does not understand that escaping, so keep the prompt
// to plain text there.
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
