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

  // Transient, never persisted: set when this session is a one-shot job run.
  // When non-empty, SessionPage sends jobCommands as-is instead of building
  // the normal agent launch command line. To close the tab once the job
  // completes, the caller appends "; exit" to the last command itself — the
  // shell then terminates on its own and the existing exit-triggered tab
  // close (OnSessionExited) takes care of the rest.
  std::vector<wxString> jobCommands;
  // The originating JobDef::name, for jobs.log attribution (a job run's
  // session name is "<jobName> #N", but the name alone isn't parsed back).
  wxString jobName;
  inline bool IsJobRun() const { return !jobCommands.empty(); }
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
