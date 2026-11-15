#pragma once

#include "core/Account.h"
#include "core/TmuxCommands.h"

#include <optional>
#include <vector>
#include <wx/string.h>

class AdapterRegistry;
struct AgentDef;

// One persisted session (workspace.json -> sessions[]).
struct Session {
  wxString name;       // unique within the workspace
  wxString agentName;  // references an AgentDef::name
  wxString workingDir; // cwd the client runs in
  wxString groupName;
  // The folder the user chose for the file explorer and the git view of the
  // session (in the notation of its host). Empty: the working folder.
  wxString sideDir;
  bool plainTerminal{false}; // This session represents a plain terminal
  // The shell a plain terminal runs. Empty: the default one (or ask, when
  // there are several). Saved so that a restored terminal does not ask again.
  wxString loginShell;
  // The name of the tmux session that the agent (or the terminal) runs in, on
  // its machine: "kennel-<group>-<name>" (tmux::SessionNameFor()). Made once,
  // when the session is created, and kept: it is what a restored session
  // attaches to, and a rename of the session must not lose it. Empty: the
  // session does not run in tmux.
  wxString tmuxName;
  // A plain terminal that runs on a remote host: the host (and the ssh user,
  // empty for the default one). The terminal is a local shell that logs in with
  // ssh (and then runs in tmux on the host), so a restored terminal needs to
  // know where to log in. Empty: a terminal of this machine (or of a WSL
  // distribution, which loginShell names). Never set for an agent session: its
  // agent says where it runs.
  wxString remoteHost;
  wxString remoteUser;

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
  // Whether this session is a one-shot job run (a scheduled job, or "Run Now"):
  // a terminal tab that types the commands of the job (`jobCommands`) as they
  // are, and not the launch command of an agent. The user did not start it by
  // hand and Kennel owns its whole life: it is closed when the job ends, it is
  // never saved (IsPersistent()), never archived, and never runs in tmux.
  inline bool IsJobRun() const { return !jobCommands.empty(); }
  // Whether the session is written to workspace.json: everything but one-shot
  // job runs (plain terminals are restored too).
  inline bool IsPersistent() const { return !IsJobRun(); }

  // Whether the agent (or the terminal) of the session runs in a tmux session
  // on its machine: it has a tmux name, and it is not a job run.
  inline bool IsTmuxSession() const { return !tmuxName.empty() && !IsJobRun(); }
  // The tmux server that holds the session: the one of the agents, or the one
  // of the plain terminals.
  tmux::Server GetTmuxServer() const;
  // The machine where the tmux session of this session is: the host of its
  // agent (a terminal: its `remoteHost`), a WSL distribution (the login shell
  // of the agent, of the terminal, or `defaultShell` when it has none), or this
  // machine. `agent` is the agent of an agent session (GetTmuxServer() says
  // which kind it is: `agent` is not looked at for a plain terminal).
  Account GetTmuxAccount(const std::optional<AgentDef> &agent,
                         const wxString &defaultShell) const;
  // Where the tmux session is, to ask or end it: nothing when the session is
  // not a tmux session (IsTmuxSession()), when its agent is not in `agents` any
  // more (a call on the terminal server of this machine would find nothing and
  // still say that it worked), or when its machine has no tmux (this machine on
  // Windows).
  std::optional<tmux::Target> GetTmuxTarget(const AdapterRegistry &agents,
                                            const wxString &defaultShell) const;
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

// One open file (workspace.json -> files[]). Only the place of the file is
// kept: edits that were not saved are not.
struct SessionFile {
  wxString path;       // A local file: its full path. A remote one: the path on
                       // the host.
  wxString remoteHost; // Empty for a local file
  wxString remoteUser; // Empty: the ssh default
};

// A session that was archived: closed, but kept so it can be restored
// (workspace.json -> archived[]).
struct ArchivedSession {
  Session session;
  // When it was archived, local time as "YYYY-MM-DD HH:MM:SS" (sorts by time).
  wxString archivedAt;
};

// The single implicit workspace and its sessions.
struct Workspace {
  int version = 1;
  std::vector<Session> sessions;
  // The open files, in the order of the last run (new ones last).
  std::vector<SessionFile> files;
  // Persistent sessions and open files, most recently used first. A file is in
  // the group of the Files container, named by its key.
  std::vector<SessionRef> recentSessions;
  // The tabs of the main book in the order of the tabs: the sessions and the
  // files (a file as in recentSessions). Pages that are not here go last.
  std::vector<SessionRef> tabOrder;
  // The archived sessions, oldest first.
  std::vector<ArchivedSession> archived;
};

// Inputs for creating a new session.
struct NewSessionRequest {
  wxString name;
  wxString agentName;
  wxString workingDir;
  bool resume{false};
  wxString groupName;
  bool plainTerminal{false};
  // The tmux session of the session when it is restored (an archived session
  // keeps its tmux session on the machine); empty: a new name is made, if the
  // session runs in tmux (see MainNotebook::LaunchSession()).
  wxString tmuxName;
  // A plain terminal on a remote host, see Session::remoteHost.
  wxString remoteHost;
  wxString remoteUser;
  // The shell of a plain terminal. Empty: the default one.
  wxString loginShell;

  // Set for a one-shot job run; see Session::jobCommands/jobName.
  std::vector<wxString> jobCommands;
  wxString jobName;
};
