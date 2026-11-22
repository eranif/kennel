#pragma once

#include "app/AsyncGuard.hpp"
#include "core/Config.h"
#include "core/Workspace.h"

#include <memory>
#include <mutex>
#include <wx/datetime.h>
#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

class wxTerminalViewCtrl;

// The folder of a session as Kennel reaches it, to read and write files there:
// a folder of this machine (also the one of a WSL distribution, through its
// share), or a folder of a remote host (over SFTP).
struct HandoffPlace {
  // Where `agent` runs `workingDir`. A WSL folder is reached through the share
  // of the distribution; empty `localDir` there means that it is not found.
  HandoffPlace(const AgentDef &agent, const wxString &workingDir);

  bool IsRemote() const { return !remoteHost.empty(); }
  // `relPath` in the folder, as Kennel reaches it (a local path, or a path of
  // the remote host).
  wxString GetPath(const wxString &relPath) const;
  // `relPath` in the folder, as the agent sees it: an absolute path of its
  // machine (a Linux path for WSL). A remote folder is absolute only once
  // SetRemoteDir() was given the resolved one.
  wxString GetAgentPath(const wxString &relPath) const;
  // The remote folder, resolved (SftpClient::ResolvePath()): absolute.
  void SetRemoteDir(const wxString &dir);
  // Whether `text` can be on the command line of the agent as it is: the
  // command line is quoted for a POSIX shell (BuildCommandLine()), which the
  // shells of Windows (cmd, PowerShell) read in another way: there `%`, `!`,
  // `$`, `` ` `` and `"` would change the text. Always true for a POSIX shell.
  bool IsSafeOnCommandLine(const wxString &text) const;
  // Keeps `pattern` out of `git status` of a local folder, without touching the
  // .gitignore of the repository (.git/info/exclude). Best effort: only a plain
  // .git folder, and not over SSH.
  void AddGitExclude(const wxString &pattern) const;

  wxString localDir; // A local folder: empty for a remote host
  // The agent is typed into a POSIX shell: a remote host, WSL, bash, or any
  // shell of macOS or Linux (see IsSafeOnCommandLine()).
  bool posixShell{true};
  // The folder as the agent sees it, absolute (see GetAgentPath()).
  wxString agentDir;
  wxString remoteHost;
  wxString remoteUser;
  wxString remoteDir; // "~" when the session has no folder
};

// Sent by a SessionHandoff to itself (its owner binds them): the summary is
// ready, GetString() is its text (wxEVT_SESSION_SUMMARY_READY); or there is
// none, and GetString() says why (wxEVT_SESSION_SUMMARY_FAILED). The page of
// the session passes READY on to the notebook (the event object is the page).
wxDECLARE_EVENT(wxEVT_SESSION_SUMMARY_READY, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SESSION_SUMMARY_FAILED, wxCommandEvent);

// "Duplicate with Summary", first part: the agent of a session writes a summary
// of the session on its machine. One object for one summary, owned by the page
// of the session (it types into the terminal of that page):
//
//   1. Begin() types a prompt into the agent: write the summary to
//      <folder>/.kennel/handoff/<id>/summary.md, then the marker
//      <folder>/.kennel/handoff/<id>/done.
//   2. It checks for the marker every second (every few seconds over SSH), for
//      5 minutes at most.
//   3. It reads the summary, deletes the folder of the handoff, and sends
//      wxEVT_SESSION_SUMMARY_READY with the text.
//
// The paths in the prompt are absolute, as the agent sees them (a remote folder
// is resolved first). The second part, after the Start Agent dialog, is
// HandoffDelivery.
class SessionHandoff : public wxEvtHandler {
public:
  // `source`: the folder of the session that writes the summary, and
  // `terminal` its agent.
  SessionHandoff(const HandoffPlace &source, wxTerminalViewCtrl *terminal);
  // A handoff that did not end yet stops (see Stop()): the owner deletes it on
  // a cancel, a restart of the terminal, or when the page goes.
  ~SessionHandoff() override;

  // Types the prompt and starts to wait for the summary (a remote folder is
  // resolved first).
  void Begin();
  // The user does not want the summary any more (see Stop()). The owner
  // deletes the handoff after it.
  void Cancel();

private:
  // The one way a handoff ends: no more checks; an agent that was asked and
  // has not finished gets Ctrl+C; the folder of the handoff is deleted (also a
  // part that the agent wrote). Once only.
  void Stop();
  // Types the prompt, and the checks start.
  void SendPrompt();
  // The folder of the handoff in the source folder, relative to it.
  wxString GetHandoffDir() const;
  wxString GetPrompt() const;
  void OnEnterTimer(wxTimerEvent &event);
  void OnPollTimer(wxTimerEvent &event);
  // Checks for the marker, and reads the summary when it is there.
  void CheckLocal();
  void CheckRemote();
  void OnSummary(const wxString &summary);
  void DeleteSourceFiles();
  void Fail(const wxString &why);

  HandoffPlace m_source;
  wxTerminalViewCtrl *m_terminal{nullptr}; // Of the owner page
  wxString m_id;
  wxDateTime m_deadline;
  wxTimer m_enterTimer;
  wxTimer m_pollTimer;
  // The prompt was typed into the agent (Stop() has something to interrupt).
  bool m_promptSent{false};
  // The marker was found: the agent finished (Stop() does not interrupt it).
  bool m_summaryFound{false};
  // A check over SSH runs: the timer does not start another one.
  bool m_ioBusy{false};
  // Stop() ran: no more checks, and a result that comes late is dropped.
  bool m_done{false};
  AliveFlag m_alive{MakeAliveFlag()};
};

// Sent by a HandoffDelivery to itself (its owner binds them): the summary is in
// the folder of the new session, and GetRequest() is the request to start it,
// with the first prompt that tells its agent to read the summary
// (wxEVT_HANDOFF_DELIVERED); or it could not be written, and GetString() says
// why (wxEVT_HANDOFF_DELIVERY_FAILED).
class HandoffDeliveryEvent : public wxCommandEvent {
public:
  explicit HandoffDeliveryEvent(wxEventType type = wxEVT_NULL)
      : wxCommandEvent(type) {}
  wxEvent *Clone() const override { return new HandoffDeliveryEvent(*this); }

  const NewSessionRequest &GetRequest() const { return m_request; }
  void SetRequest(const NewSessionRequest &request) { m_request = request; }

private:
  NewSessionRequest m_request;
};

wxDECLARE_EVENT(wxEVT_HANDOFF_DELIVERED, HandoffDeliveryEvent);
wxDECLARE_EVENT(wxEVT_HANDOFF_DELIVERY_FAILED, HandoffDeliveryEvent);

// "Duplicate with Summary", second part: writes the summary to
// kennel-handoff-<id>.md in the folder of the new session, on its machine (its
// agent is asked to delete it when it has read it), then sends
// wxEVT_HANDOFF_DELIVERED with the request of the session. The name is new for
// each delivery, and the file is created only when nothing is at that path
// (not even a symbolic link): a file of the user is never replaced. One object
// for one new session, owned by the notebook (it starts the session).
class HandoffDelivery : public wxEvtHandler {
public:
  HandoffDelivery(const HandoffPlace &target, const wxString &summary,
                  const NewSessionRequest &request);
  ~HandoffDelivery() override;

  void Begin();
  // The new session did not start: the summary that was written is deleted
  // (no agent will read it and delete it). The destructor does it too, for a
  // summary that was not handed over.
  void Undo();
  // The page of the new session has the summary now (Session::promptFile): the
  // delivery does not delete it.
  void ReleaseFile();

private:
  // `agentPath`: the summary as the agent of the new session sees it, and
  // `writtenPath` as Kennel reaches it (for Undo()).
  void Succeed(const wxString &agentPath, const wxString &writtenPath);
  void Fail(const wxString &why);

  // What the worker of a remote write and this delivery share: whichever comes
  // last deletes a file that nobody took (the delivery went while the worker
  // wrote, or before the result of the worker came to it).
  struct RemoteWrite {
    std::mutex mutex;
    bool ownerGone{false};
    wxString path;       // The file that the worker wrote
    bool handled{false}; // Its result came to the delivery
  };

  HandoffPlace m_target;
  wxString m_summary;
  NewSessionRequest m_request;
  std::shared_ptr<RemoteWrite> m_remoteWrite{std::make_shared<RemoteWrite>()};
  // The name of the summary in the folder: kennel-handoff-<id>.md.
  wxString m_fileName;
  // The summary that was written, as Kennel reaches it; empty before.
  wxString m_writtenPath;
  AliveFlag m_alive{MakeAliveFlag()};
};
