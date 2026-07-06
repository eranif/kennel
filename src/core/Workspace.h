#pragma once

#include <wx/string.h>

#include <vector>

// One persisted session (workspace.json -> sessions[]).
struct Session {
  wxString name;       // unique within the workspace
  wxString agentName;  // references an AgentDef::name
  wxString workingDir; // cwd the client runs in
  wxString groupName;
  bool plainTerminal{false}; // This session represents a plain terminal
  wxString loginShell;
};

// The single implicit workspace and its sessions.
struct Workspace {
  int version = 1;
  std::vector<Session> sessions;
};
