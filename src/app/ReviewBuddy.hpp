#pragma once

#include "app/AsyncGuard.hpp"
#include "core/ReviewLoop.h"

#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class wxTerminalViewCtrl;

wxDECLARE_EVENT(wxEVT_REVIEW_CHANGED, wxCommandEvent);

// Runs a ReviewLoop for one SessionPage: the main agent's terminal plus the
// reviewer's terminal next to it. It writes the request files and checks every
// second (every few seconds over SSH) for the marker the loop waits for. The
// first request goes to the reviewer on its command line, when the owner
// starts it (see LaunchFn); later requests, and those for the main agent, are
// typed into the running terminals. It never owns the terminals.
class ReviewBuddy : public wxEvtHandler {
public:
  struct Target {
    wxString workingDir;
    wxString remoteHost; // Empty: the files are local
    wxString remoteUser;
    wxString sessionName; // For the notifications
  };

  // Starts the reviewer's agent with `prompt` as its first message and returns
  // its terminal (nullptr if that failed). The agent runs in `folder` (relative
  // to the working dir): its history is then apart from the main agent's.
  using LaunchFn = std::function<wxTerminalViewCtrl *(const wxString &prompt,
                                                      const wxString &folder)>;

  // Gives the keyboard focus to a terminal, if the user is working in this
  // session. Called when a request is typed into a terminal: that agent is the
  // active one now, and it may need an answer (a permission prompt).
  using FocusFn = std::function<void(wxTerminalViewCtrl *terminal)>;

  // `isShown` tells whether the user is looking at the main agent's session
  // right now; it decides whether a system notification is worth showing.
  ReviewBuddy(const Target &target, wxTerminalViewCtrl *main,
              LaunchFn launchReviewer, std::function<bool()> isShown,
              FocusFn focusTerminal);
  ~ReviewBuddy() override;

  // Writes the first request and starts the reviewer with it.
  void Begin();

  // The user can act on the loop only when it has started.
  bool IsRunning() const;
  bool HasStalled() const;
  // The review has not ended yet: it is starting, running or stalled (waiting
  // for the user). False once it is finished or the user stopped it.
  bool IsInProgress() const;
  // A file is being written or checked over SSH; Resend() waits for that.
  bool IsBusy() const { return m_ioBusy; }
  wxString Describe() const;
  // A short state for the status bar, e.g. "Waiting for review (round 1/5)" or
  // "Addressing comments (round 1/5)".
  wxString StatusText() const;
  // Where wxEVT_REVIEW_CHANGED is sent whenever the state may have changed
  // (StatusText() is up to date). The event object is this ReviewBuddy.
  void SetEventTarget(wxEvtHandler *target) { m_eventTarget = target; }
  void Resend();
  void Stop();
  // The comments file of the current round, relative to the working dir; empty
  // before the loop has started. The file exists until the reviewer's pane is
  // closed: after a clean review, the review folder is deleted then.
  wxString CommentsPath() const;
  const Target &GetTarget() const { return m_target; }

private:
  using Actions = std::vector<ReviewLoop::Action>;

  void OnPoll(wxTimerEvent &);
  void OnEnterTimer(wxTimerEvent &);

  void Execute(Actions actions, size_t from = 0);
  void PasteLine(wxTerminalViewCtrl *terminal, const wxString &line);
  void Finished();
  void NotifyChanged();
  // Tells the user about the loop: when they are not looking at this session, a
  // system notification. The Dock icon / taskbar button also asks for attention
  // when Kennel is in the background. (The status bar follows the state of the
  // loop through wxEVT_REVIEW_CHANGED, see SessionPage.)
  void NotifyUser(const wxString &title, const wxString &message);

  bool WriteLocal(const wxString &relPath, const wxString &text);
  wxString ReadLocal(const wxString &relPath) const;
  wxString LocalPath(const wxString &relPath) const;
  wxString RemotePath(const wxString &relPath) const;
  void AddIgnoreRule() const;

  void CheckRemote(const wxString &marker, const wxString &fileToRead);
  // Deletes the loop's folder (and the .agents folders above it if they are
  // empty now). True if the folder is gone. Failing is only logged.
  bool RemoveLocal(const wxString &relPath);
  // Same over SSH, in the background: nothing waits for it.
  void RemoveRemote(const wxString &relPath);
  void WriteRemote(const wxString &relPath, const wxString &text, Actions rest,
                   size_t next);

  Target m_target;
  bool m_remote;
  wxTerminalViewCtrl *m_main;
  wxTerminalViewCtrl *m_reviewer{nullptr}; // Null until the reviewer started
  LaunchFn m_launchReviewer;
  std::function<bool()> m_isShown;
  FocusFn m_focusTerminal;
  wxEvtHandler *m_eventTarget{nullptr};

  std::unique_ptr<ReviewLoop> m_loop;
  wxTimer m_pollTimer;
  wxTimer m_enterTimer;
  wxTerminalViewCtrl *m_enterTarget{nullptr};
  std::chrono::steady_clock::time_point m_lastProgress;
  int m_remoteErrors{0};
  bool m_ioBusy{false};
  bool m_stopped{false}; // Stop() was called (also before Begin())
  // The folder to delete when this object goes away (the reviewer runs in it,
  // so it stays until its pane is closed). Empty: none.
  wxString m_removeOnClose;

  // For the worker threads of the remote checks, see AsyncGuard.hpp.
  AliveFlag m_alive{MakeAliveFlag()};
};
