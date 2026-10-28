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
  // The shell a plain terminal runs. Empty: the default one (or ask, when
  // there are several). Saved so that a restored terminal does not ask again.
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
  // Whether the session is written to workspace.json: everything but one-shot
  // job runs (plain terminals are restored too).
  inline bool IsPersistent() const { return !IsJobRun(); }
};

// Names a page of the main view by strings, never by pointer. A session:
// `name` is the session name, `groupName` its group (names are only unique
// within a group). A file: `name` is the file key, `groupName` the Files
// container.
struct SessionRef {
  wxString groupName;
  wxString name;

  bool operator==(const SessionRef &) const = default;
};

// Persisted per-group metadata not tied to any single session (e.g. the
// group's icon), keyed by group name.
struct GroupMeta {
  wxString name;
  wxString icon; // Bitmaps alias, e.g. "group-red"
};

// One open file (workspace.json -> files[]). Only the place of the file is
// kept: edits that were not saved are not.
struct SessionFile {
  wxString path;       // A local file: its full path. A remote one: the path on
                       // the host.
  wxString remoteHost; // Empty for a local file
  wxString remoteUser; // Empty: the ssh default
};

// The single implicit workspace and its sessions.
struct Workspace {
  int version = 1;
  std::vector<Session> sessions;
  std::vector<GroupMeta> groups;
  // The open files, in the order of the last run (new ones last).
  std::vector<SessionFile> files;
  // Persistent sessions and open files, most recently used first. A file is in
  // the group of the Files container, named by its key.
  std::vector<SessionRef> recentSessions;
};

// Inputs for creating a new session.
struct NewSessionRequest {
  wxString name;
  wxString agentName;
  wxString workingDir;
  bool resume{false};
  wxString groupName;
  bool plainTerminal{false};

  // Set for a one-shot job run; see Session::jobCommands/jobName.
  std::vector<wxString> jobCommands;
  wxString jobName;
};
