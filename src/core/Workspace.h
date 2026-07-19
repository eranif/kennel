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

// Persisted per-group metadata not tied to any single session (e.g. the
// group's icon), keyed by group name.
struct GroupMeta {
  wxString name;
  wxString icon; // Bitmaps alias, e.g. "group-red"
};

// The single implicit workspace and its sessions.
struct Workspace {
  int version = 1;
  std::vector<Session> sessions;
  std::vector<GroupMeta> groups;
};
