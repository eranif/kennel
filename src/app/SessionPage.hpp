#ifndef SESSIONPAGE_HPP
#define SESSIONPAGE_HPP
#include "UI.hpp"
#include "app/AcceleratorInterceptor.h"
#include "app/AsyncGuard.hpp"
#include "app/ReviewBuddyEvent.hpp"
#include "app/SessionStatusEvent.hpp"
#include "core/ActivityMonitor.h"
#include "core/AppPaths.h"
#include "core/Config.h"
#include "core/Workspace.h"
#include <optional>

#include "terminal_theme.h"

#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include <wx/timer.h>

class wxTerminalViewCtrl;
class wxTerminalEvent;
class wxSplitterWindow;
class wxPanel;
class wxContextMenuEvent;
class ReviewBuddy;
class ReviewBuddyHeader;
class SearchBar;
class wxAuiToolBar;
class wxMenu;
class wxUpdateUIEvent;

enum class SessionStatus { Starting, Running, Idle, Exited, Error };

class SessionPage : public SessionBasePage {
public:
  using StatusChangedFn = std::function<void(SessionStatus)>;

  SessionPage(wxSimplebook *parent, std::optional<AgentDef> agent,
              Session session, bool resume = false);
  ~SessionPage() override;

  // What the program in the agent's terminal reports with OSC 7501: "working"
  // gets a spinner on the page's row, "done" a check mark and "blocked" a
  // hand. Everything else is None.
  enum class ProgramActivity {
    None,
    Working,
    Done,
    Blocked,
  };
  ProgramActivity GetProgramActivity() const { return m_programActivity; }
  // The user has seen that the program is done: the check mark goes away.
  void AcknowledgeDone() {
    if (m_programActivity == ProgramActivity::Done) {
      SetProgramActivity(ProgramActivity::None);
    }
  }

  SessionStatus Status() const { return m_status; }
  const Session &GetSession() const { return m_session; }
  Session &GetSession() { return m_session; }
  // The folder the shell is in now, or the one the session started in.
  wxString GetCurrentDir() const;
  inline bool IsPlainTerminal() const { return !m_agent.has_value(); }
  void Restart();
  // The agent's own terminal.
  wxTerminalViewCtrl *GetTerminal() { return m_terminal; }
  // The agent's terminal, plus the review buddy's while it is open.
  std::vector<wxTerminalViewCtrl *> GetTerminals() const;
  void ApplyTheme(const wxTerminalTheme &theme);
  // Gives the toolbar the style of the theme of the application (see
  // toolbar::ApplyStyle()). Called when the theme changes.
  void ApplyToolBarStyle();
  // Focuses the terminal that had the focus last (the agent's by default).
  void SetFocus() override;

  bool IsActive() const;
  void SetDefaultSessionName(const wxString &name);
  void ApplyTitle();
  // Sends what the status bar of the main window should show (only when this
  // page is showing). Also called when the page is selected.
  void PublishStatus();

  // Shows the search bar below the terminals. It searches the terminal that
  // has the focus (or had it last). The selected text of the terminal is what
  // it looks for.
  void ShowFind();
  // Selects the next (or previous) match of the last search. Shows the search
  // bar if there was no search yet.
  void FindNext(bool forward);
  // Whether there is a search for FindNext() to repeat.
  bool HasSearch() const { return !m_findText.empty(); }

private:
  void CreateTerminal();
  // Everything both terminals need once they exist: theme, scrollback, links,
  // the context menu.
  void ConfigureTerminal(wxTerminalViewCtrl *terminal);
  // Gives `terminal` the keyboard focus, but only when the user is working in
  // this session: not when another page is showing, not when the focus is
  // elsewhere in the application (the tree, a dialog, ...), and not while the
  // user is typing in the session's other terminal.
  void FocusTerminal(wxTerminalViewCtrl *terminal);
  // A panel for the splitter that gives a terminal a border in the theme's
  // background color. Create the terminal with the pane as its parent, then
  // AddToPane() it.
  wxPanel *NewTerminalPane();
  void AddToPane(wxPanel *pane, wxTerminalViewCtrl *terminal);
  void SetStatus(SessionStatus status);
  void OnProgramStatus(wxTerminalEvent &evt);
  // Tells MainView (wxEVT_SESSION_PROGRAM) when the activity changed.
  void SetProgramActivity(ProgramActivity activity);
  // ---- Status (shown in the status bar of the main window) --------------
  // Works out where the agent runs and the session label.
  void InitStatus();
  void UpdateSessionLabel();
  // Shows "agent name | terminal title | working directory" in the header of
  // the agent's terminal. `force` shows it again when the text did not change.
  // While a review buddy is open, ReviewBuddyHeader owns the header.
  void UpdateHeader(bool force = false);
  void OnCwdChanged(wxTerminalEvent &evt);
  // Fills the title (the terminal title; not followed while a review is in
  // progress), the review text and the busy flag, then publishes. The review
  // text is:
  //   - while a review is in progress: the review state;
  //   - for a while after it ended, or after a problem: the notice;
  //   - otherwise: empty.
  void UpdateStatus();
  // Shows `text` in the review text for a while (see UpdateStatus()).
  void ShowNotice(const wxString &text);
  void ClearNotice();
  // A status change of the review buddy: the status bar shows it (and the
  // header, see ReviewBuddyHeader).
  void OnReviewChanged(ReviewBuddyEvent &event) {
    // Skipped: the header of the review gets the event too.
    event.Skip();
    UpdateStatus();
  }
  void OnNoticeTimer(wxTimerEvent &) {
    // Does not depend on the port stopping a one-shot timer before this runs.
    m_noticeTimer.Stop();
    UpdateStatus();
  }
  void OnTerminated(wxTerminalEvent &evt);
  void OnTitleChanged(wxTerminalEvent &evt);
  void OnTerminalLink(wxTerminalEvent &evt);

  // The search bar sends these to its parent, this page.
  void OnSearchBarFind(wxCommandEvent &event);
  void OnSearchBarClose(wxCommandEvent &event);
  // The terminal the search works on.
  wxTerminalViewCtrl *SearchTerminal() const {
    return m_lastFocused != nullptr ? m_lastFocused : m_terminal;
  }
  void FindIn(bool forward);

  // ---- Toolbar and review buddy ----------------------------------------
  // The terminals have no context menu: the actions are in the toolbar.
  // This handler only hides the terminal's own menu.
  void OnTerminalContextMenu(wxContextMenuEvent &evt);
  void CreateToolBar();
  // The terminal the Paste and Find buttons work on: the one with the focus,
  // or the one that had it last.
  wxTerminalViewCtrl *ActionTerminal() const;
  // The terminal the Copy button works on: one that has a selection.
  wxTerminalViewCtrl *CopyTerminal() const;
  void OnToolRestart(wxCommandEvent &event);
  void OnToolReviewBuddy(wxCommandEvent &event);
  void OnToolCopy(wxCommandEvent &event);
  void OnToolPaste(wxCommandEvent &event);
  void OnToolFind(wxCommandEvent &event);
  void OnToolCopyUI(wxUpdateUIEvent &event);
  void OnToolHasTerminalUI(wxUpdateUIEvent &event);
  void OnToolReviewBuddyUI(wxUpdateUIEvent &event);
  // The items that manage a review buddy that is open.
  void AppendReviewControls(wxMenu &menu);
  // Agents the review buddy can be: the ones on the same host as this session.
  std::vector<AgentDef> GetReviewerCandidates() const;
  // Whether this session can have a review buddy at all, and if not, why. May
  // start the git check again when an earlier one failed.
  bool CanHaveReviewBuddy(wxString &whyNot);
  // Looks for a .git in the working directory (over SFTP for a remote one).
  void CheckGitRepo();
  void CheckWslGitRepo();
  // The session's working directory as Kennel itself can reach it: the same
  // for a local agent, the distro's file share (or the Windows drive) for a WSL
  // agent, whose directory is a Linux path. Empty if it cannot be worked out.
  // For a remote agent it is the path on the remote host.
  wxString HostWorkingDir() const;
  // Looks again when the answer may have changed; see the definition.
  void RefreshGitState();
  void LaunchReviewBuddy(const AgentDef &reviewer);
  // Opens the reviewer's pane and starts its agent with `prompt` as the first
  // message. Called by ReviewBuddy once the request file is written.
  // The agent runs in `folder`, relative to the working dir.
  wxTerminalViewCtrl *StartReviewer(const AgentDef &reviewer,
                                    const wxString &prompt,
                                    const wxString &folder);
  // Destroys the reviewer's pane and terminal; no UI work around it.
  void DestroyReviewPane();
  void CloseReviewBuddy();
  void OpenLatestReview();
  wxBookCtrlBase *GetBook() const {
    return dynamic_cast<wxBookCtrlBase *>(GetParent());
  }

  enum {
    ID_RESTART = wxID_HIGHEST + 1000,
    ID_REVIEW_BUDDY,
    ID_COPY,
    ID_PASTE,
    ID_FIND,
  };
  wxAuiToolBar *m_toolbar{nullptr};

  AppPaths m_paths;
  std::optional<AgentDef> m_agent{std::nullopt};
  Session m_session;
  bool m_resume = false;

  enum class GitState { Unknown, Yes, No };

  // What the status bar of the main window shows while this page is showing.
  wxString m_statusText;   // The terminal title
  wxString m_reviewText;   // The state of the review buddy, or a notice
  wxString m_host;         // Where the agent runs
  wxString m_sessionLabel; // "session - agent"
  wxBitmapBundle m_sessionIcon;
  bool m_busy{false}; // A review is working
  wxString m_notice;
  wxTimer m_noticeTimer{this};
  bool m_endNoticeShown{false}; // For the review that ended

  wxString m_findText;             // What the last search looked for
  SearchBar *m_searchBar{nullptr}; // Hidden until Find

  // The agent's pane is the only child of the splitter until a review buddy
  // opens its pane next to it. Each pane holds one terminal with a border.
  wxSplitterWindow *m_splitter{nullptr};
  wxPanel *m_mainPane{nullptr};
  wxPanel *m_reviewPane{nullptr};
  wxTerminalViewCtrl *m_terminal{nullptr};
  wxTerminalViewCtrl *m_reviewTerminal{nullptr};
  wxTerminalViewCtrl *m_lastFocused{nullptr};
  // The terminal the user typed in last, and when (see FocusTerminal()).
  wxTerminalViewCtrl *m_lastKeyTerminal{nullptr};
  std::chrono::steady_clock::time_point m_lastKeyTime;
  std::unique_ptr<AcceleratorInterceptor> m_reviewAcceleratorInterceptor;
  std::unique_ptr<ReviewBuddy> m_review;
  std::unique_ptr<ReviewBuddyHeader> m_reviewHeader;
  GitState m_gitState{GitState::Unknown};
  bool m_gitCheckBusy{false};
  std::chrono::steady_clock::time_point m_lastGitCheck;
  // For the worker threads, see AsyncGuard.hpp.
  AliveFlag m_alive{MakeAliveFlag()};
  std::unique_ptr<ActivityMonitor> m_monitor;
  SessionStatus m_status = SessionStatus::Starting;
  ProgramActivity m_programActivity{ProgramActivity::None};
  wxString m_defaultTitle;  // The window title
  wxString m_terminalTitle; // The title the terminal set; in the status bar
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor{nullptr};
};

wxDECLARE_EVENT(wxEVT_SESSION_IDLE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SESSION_ACTIVE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SESSION_EXITED, wxCommandEvent);
// The folder of a page changed. The event object is the page.
wxDECLARE_EVENT(wxEVT_SESSION_CWD, wxCommandEvent);
// The program activity of a page changed (see SessionPage::ProgramActivity).
// The event object is the page; read the state from it.
wxDECLARE_EVENT(wxEVT_SESSION_PROGRAM, wxCommandEvent);
#endif // SESSIONPAGE_HPP
