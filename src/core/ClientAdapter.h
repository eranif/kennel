#pragma once

#include <wx/string.h>

#include "core/Config.h"

#include <vector>

// Builds the list of commands to send to the terminal to launch a session.
// When resume is true and agent.resumeArg is non-empty, the resume arg is
// appended before agent.extraArgs.
std::vector<wxString> BuildCommandLine(const AgentDef &agent,
                                       const wxString &workingDir, bool resume);
