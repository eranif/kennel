#ifndef SESSIONPAGE_HPP
#define SESSIONPAGE_HPP
#include "UI.hpp"
#include "app/AcceleratorInterceptor.h"
#include "app/AsyncGuard.hpp"
#include "app/ReviewBuddyEvent.hpp"
#include "app/SearchablePage.hpp"
#include "app/SessionHandoff.hpp"
#include "app/SessionStatusEvent.hpp"
#include "app/TabIcon.h"
#include "app/TmuxMachines.hpp"
#include "core/Account.h"
#include "core/AppPaths.h"
#include "core/Config.h"
#include "core/PageViewEvent.hpp"
#include "core/Workspace.h"
#include <optional>

#include "terminal_theme.h"

#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include <wx/infobar.h>
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

// Detached: the terminal ended, but the tmux session of the session is still
// running on its machine (the user detached, or the connection dropped). The
// tab stays; Restart() attaches again.
enum class SessionStatus { Starting, Running, Exited, Error, Detached };

class SessionPage : public SessionBasePage, public SearchablePage {
public:
  using StatusChangedFn = std::function<void(SessionStatus)>;

  // `owner` is for Clone() only: the page that holds this one in its bottom
  // pane.
  SessionPage(wxWindow *parent, std::optional<AgentDef> agent, Session session,
              bool resume = false, SessionPage *owner = nullptr);
  ~SessionPage() override;

  // A new page, a child of `parent` (which owns it), with a terminal that runs
  // where this session runs: the same machine (this machine, the WSL
  // distribution, or the remote host: it logs in with ssh), shell and folder,
  // the same name and group, and, when this session runs in tmux, in a tmux
  // session of its own on the same machine (the name of this one plus
  // "-terminal": attaching to this one would show the agent). It starts the
  // terminal (and tmux) only: no agent is launched, also not by Restart(). The
  // clone is a plain terminal; the caller names it and keeps it (it is not in
  // the workspace).
  SessionPage *Clone(wxWindow *parent);

  // The bottom pane with a terminal on the machine and in the folder of this
  // session (a Clone()), shown or hidden: the first time it creates the
  // terminal and shows the pane, then it hides it (the terminal goes on
  // running) and shows it again. The end of the terminal closes the pane and
  // forgets the terminal. Only an agent has it (CanToggleTerminalPane()); it is
  // the Terminal button of the toolbar, and the Ctrl+` key.
  bool CanToggleTerminalPane() const;
  void ToggleTerminalPane();

  // What the program in the agent's terminal reports with OSC 7501: "working"
  // gets a spinner on the page's row, "done" a check mark and "blocked" a
  // hand. Everything else is None. While a review buddy is open, the page
  // shows what both terminals report: Blocked wins over Working, and Working
  // over Done.
  using ProgramActivity = PageActivity;
  ProgramActivity GetProgramActivity() const { return m_programActivity; }
  // The user has seen that the program is done: the check mark goes away.
  void AcknowledgeDone();

  SessionStatus Status() const { return m_status; }

  // "Duplicate with Summary": the agent of this session writes a summary of
  // it on its machine (see SessionHandoff). The message bar says that it waits,
  // with a Cancel button. When the summary is there, the page sends
  // wxEVT_SESSION_SUMMARY_READY to the notebook (GetString() is the summary,
  // the event object is the page), which asks for the new session.
  void StartHandoff();
  // An agent session whose agent was started in its terminal and did not end.
  bool IsAgentRunning() const;
  // Types `text` (one line) into the agent, then Enter: File > Broadcast.
  // False, and nothing is typed, when the agent does not run, or waits for an
  // answer (OSC 7501: the text would be the answer).
  bool SendPrompt(const wxString &text);
  // Whether StartHandoff() can be used now: an agent session whose agent was
  // started in a terminal and did not end, that does not work and does not
  // wait for an answer (OSC 7501), and no other handoff waits.
  bool CanStartHandoff() const;
  // The dot on the tab of the session: it runs in tmux, or it is Detached.
  TabBadge GetTabBadge() const;
  const Session &GetSession() const { return m_session; }
  Session &GetSession() { return m_session; }
  // The folder the shell is in now, or the one the session started in.
  wxString GetCurrentDir() const;
  // Where the files of the session are: this machine, the WSL distribution or
  // the remote host the program runs on, starting in the folder of the session.
  Account GetAccount() const;
  inline bool IsPlainTerminal() const { return !m_agent.has_value(); }
  // kTerminal for a plain terminal, kSession for a session of an agent.
  PageType GetPageType() const {
    return IsPlainTerminal() ? PageType::kTerminal : PageType::kSession;
  }
  void Restart();
  // Types `commands` into the terminal, once what the session runs is started
  // (in tmux, that is after the machine has answered): a command typed before
  // would go to the shell that tmux is started from. Typed at once when it is
  // started. They stay through Restart(): the new terminal types them.
  void SendCommands(const std::vector<wxString> &commands);
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

  // SearchablePage. The search bar is below the terminals, and searches the
  // terminal that has the focus (or had it last). GetFindWhat() is the text
  // selected in the terminal: a TUI application (tmux, vim, claude) selects
  // text itself, and the terminal does not know it, so the text that it copied
  // (OSC 52) comes first, then the text selected with the mouse.
  // GetSearchTarget() is the account of the session and its folder.
  bool CanFind() const override { return true; }
  void ShowFind() override;
  bool CanFindNext() const override { return !m_findText.empty(); }
  void FindNext(bool forward) override;
  wxString GetFindWhat() const override;
  SearchTarget GetSearchTarget() const override;

  // ---- Review buddy, for the commands outside of the toolbar -------------
  // The agents this session can be reviewed with now. Empty if it cannot (a
  // terminal, no git repository, a review buddy is open already...).
  std::vector<AgentDef> GetReviewBuddyChoices();
  // Starts a review buddy with the agent `agentName` (one of the choices).
  void StartReviewBuddy(const wxString &agentName);
  bool HasReviewBuddy() const { return m_review != nullptr; }
  // Closes the review buddy that is open.
  void StopReviewBuddy();
  // Whether this is a session that can have a review buddy at all: an agent
  // that runs (not a plain terminal or a scheduled job).
  bool SupportsReviewBuddy() const;
  // Asks which agent reviews this session (a list in a dialog), then starts the
  // review buddy with it. Tells the user why when it can not be started now.
  // Shows a dialog: call it after the handler of a menu or a button has ended
  // (CallAfter()).
  void ChooseReviewBuddy();
  // Why a review buddy can not be started now (an agent can have one if it is
  // in a git repository and another agent runs on the same host); empty when it
  // can. In that case `reviewers`, when given, gets the agents it can be
  // started with (the same as GetReviewBuddyChoices(), without looking twice).
  wxString
  GetReviewBuddyUnavailableReason(std::vector<AgentDef> *reviewers = nullptr);
  // What the open review buddy is doing; empty when none is open.
  wxString DescribeReviewBuddy() const;
  // The open review buddy is running or has stalled: it can be sent the request
  // again or be stopped.
  bool IsReviewBuddyLive() const;
  // The request can be sent again now (nothing is being written or checked).
  bool CanResendReview() const;
  void ResendReview();
  // Stops the review, and leaves the review buddy open.
  void StopReview();
  // Whether the open review buddy has written a review to open.
  bool HasLatestReview() const;
  // Opens the file of the latest review.
  void OpenLatestReview();

private:
  // Starting a session takes two calls, in this order, from the caller (the
  // constructor, Restart()):
  // Phase 1: creates the terminal and starts its shell. The page has a terminal
  // after it, unless the user cancelled the choice of the shell.
  void CreateTerminal();
  // Phase 2: starts what runs in the terminal. A session that runs in tmux
  // needs its machine to be ready (TmuxMachines, on worker threads: one
  // question for all the sessions of a machine): it asks with an event, and
  // OnTmuxMachine() goes on when the machine has answered. The others go on at
  // once. Both end in FinaliseTerminalCreate().
  void StartProgram();
  // Types the commands of the session into the terminal (ProgramCommands()).
  void FinaliseTerminalCreate();
  // What is typed into the shell: the agent (resume or not, in tmux or not),
  // the commands of a job, or the attach of tmux for a terminal. Nothing for a
  // terminal without tmux.
  std::vector<wxString> ProgramCommands() const;
  // The session is not in tmux (any more): forgets the name, saved at once.
  void DropTmux();
  // A text in the strip at the bottom of the terminal, in place of the header,
  // until it is replaced or cleared (an empty text): "Connecting to ...".
  void SetNote(const wxString &text);
  // The machine that holds the tmux session of this session (a tmux session
  // only).
  Account GetTmuxMachine() const;
  // What TmuxMachines says about a machine (every page gets it, and the one
  // that waits for that machine goes on).
  void OnTmuxMachine(TmuxMachineEvent &event);
  // The message bar of the page (a question, or why the session can not start)
  // with the buttons that the text needs: Download (tmux-shim) and Close
  // session. It is a bar, not a dialog, so that every page can ask its own
  // question at the same time.
  void ShowInfo(const wxString &text, int icon, bool download, bool close);
  void HideInfo();
  void OnTmuxDownload(wxCommandEvent &event);
  void OnTmuxClose(wxCommandEvent &event);
  // The handoff ended (see StartHandoff()). Its object goes after its event.
  void OnHandoffReady(wxCommandEvent &event);
  void OnHandoffFailed(wxCommandEvent &event);
  // The Cancel button of the message bar.
  void OnHandoffCancel(wxCommandEvent &event);
  void OnPromptEnterTimer(wxTimerEvent &event);
  // Deletes Session::promptFile when the page goes (it is gone already when the
  // agent deleted it, as its first message asks). Not for an agent in tmux
  // whose first message was typed: it may go on without the page (Archive,
  // Kennel closing), and may still read the file. When its tmux session ends,
  // the file goes too: a Close (MainNotebook::StopTmux()), or the program that
  // ended (SetStatus(Exited)).
  void DeletePromptFile();
  void EndHandoff();
  // Everything both terminals need once they exist: theme, scrollback, links,
  // the context menu.
  void ConfigureTerminal(wxTerminalViewCtrl *terminal);
  // Gives `terminal` the keyboard focus, but only when the user is working in
  // this session: not when another page is showing, not when the focus is
  // elsewhere in the application (the list of the sessions, a dialog, ...), and
  // not while the user is typing in the session's other terminal.
  void FocusTerminal(wxTerminalViewCtrl *terminal);
  // Puts `terminal` in `pane`, one of the panes that the base page has
  // (GetAgentPanel(), ...): create the terminal with the pane as
  // its parent, then call this.
  void AddToPane(wxPanel *pane, wxTerminalViewCtrl *terminal);
  void SetStatus(SessionStatus status);
  void OnProgramStatus(wxTerminalEvent &evt);
  /// Handle OSC-52 events
  void OnAppSelectionChanged(wxTerminalEvent &event);
  // Tells MainView (wxEVT_SESSION_PROGRAM) when the activity changed.
  void SetProgramActivity(ProgramActivity activity);
  // Tells MainView to draw the icon of the tab again (the badge changed): it is
  // the event of the activity, which draws the icon.
  void RefreshTabIcon();
  // Works out the activity of the page from the one of each terminal.
  void UpdateProgramActivity();
  // "Reviewer (Review Buddy) | Reviewing changes" (or "Waiting", while the
  // main agent addresses the comments) in the header of the review buddy's
  // terminal. `force` shows it again when the text did not change.
  void UpdateReviewHeader(bool force = false);

  void UpdateHeaderColours(wxTerminalViewCtrl *ctrl);

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
    // The reviewer works on a round. Otherwise it is idle: it waits for the
    // main agent to address the comments, or for the user after the review
    // ended or stalled.
    const wxEventType type = event.GetEventType();
    m_reviewerWorking = type == wxEVT_REVIEW_BUDDY_STARTED ||
                        type == wxEVT_REVIEW_BUDDY_WAITING_FOR_FINDINGS;
    UpdateReviewHeader();
    UpdateStatus();
  }
  void OnNoticeTimer(wxTimerEvent &) {
    // Does not depend on the port stopping a one-shot timer before this runs.
    m_noticeTimer.Stop();
    UpdateStatus();
  }
  void OnTerminated(wxTerminalEvent &evt);
  // The terminal of a session that runs in tmux ended: asks its machine (on a
  // worker thread) whether the tmux session is still there. Exited when it is
  // not, Detached when it is, or when the machine could not be asked (the
  // session must not be lost because the host is out of reach).
  void CheckTmuxSessionAfterTerminated();
  void OnTmuxSessionChecked(int generation, bool answered, bool running,
                            const wxString &why);
  void OnTitleChanged(wxTerminalEvent &evt);
  void OnTerminalLink(wxTerminalEvent &evt);

  // The search bar sends these to its parent, this page.
  void OnSearchBarFind(wxCommandEvent &event);
  void OnSearchBarClose(wxCommandEvent &event);
  // The terminal the search works on.
  wxTerminalViewCtrl *SearchTerminal() const { return m_terminal; }
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
  // ToggleTerminalPane().
  void OnToolTerminal(wxCommandEvent &event);
  // Shows or hides the bottom pane, and makes its terminal if there is none:
  // after the pane is shown (CreateTerminalPage()), so that it has its size.
  // `focus` gives the focus to the terminal that is in front.
  void SetTerminalPane(bool shown, bool focus);
  // Makes the terminal of the bottom pane (a Clone()). Does nothing when there
  // is one. A terminal that cannot be made closes the pane.
  void CreateTerminalPage(bool focus);
  // Keeps what the workspace saves of the pane (Session::terminalPane) in step
  // with the pane, and asks for the workspace to be saved when it changed.
  void SaveTerminalPane();
  // Tells the notebook (the parent) about the session: the events of
  // wxEVT_SESSION_*. A page in a bottom pane tells only its owner that it
  // ended.
  void Notify(wxCommandEvent &event);
  // The terminal of the bottom pane ended: destroys its page and closes the
  // pane.
  void CloseTerminalPage();
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
  wxBookCtrlBase *GetBook() const {
    return dynamic_cast<wxBookCtrlBase *>(GetParent());
  }

  enum {
    ID_RESTART = wxID_HIGHEST + 1000,
    ID_REVIEW_BUDDY,
    ID_TERMINAL,
    ID_COPY,
    ID_PASTE,
    ID_FIND,
    ID_TMUX_DOWNLOAD,
    ID_TMUX_CLOSE,
    ID_HANDOFF_CANCEL,
  };
  wxAuiToolBar *m_toolbar{nullptr};

  AppPaths m_paths;
  std::optional<AgentDef> m_agent{std::nullopt};
  Session m_session;
  bool m_resume = false;
  // The page that holds this one in its bottom pane (Clone()); null for a page
  // of the notebook. A page with an owner is not known to the notebook: see
  // Notify().
  SessionPage *m_owner{nullptr};

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

  // The panes and splitters are the ones of the base page (wxCrafter): the
  // agent's pane is the only one open until a review buddy opens its pane next
  // to it. Each pane holds one terminal.
  wxTerminalViewCtrl *m_terminal{nullptr};
  wxTerminalViewCtrl *m_reviewTerminal{nullptr};
  // The page in the bottom pane (a clone of this one), when it is open. The
  // pane owns it.
  SessionPage *m_terminalPage{nullptr};
  std::unique_ptr<AcceleratorInterceptor> m_reviewAcceleratorInterceptor;
  std::unique_ptr<ReviewBuddy> m_review;
  // The handoff that waits for the summary (see StartHandoff()).
  std::unique_ptr<SessionHandoff> m_handoff;
  // Enter after the text of SendPrompt().
  static constexpr int kPromptEnterDelayMs = 400;
  wxTimer m_promptEnterTimer;
  std::unique_ptr<ReviewBuddyHeader> m_reviewHeader;
  GitState m_gitState{GitState::Unknown};
  bool m_gitCheckBusy{false};
  std::chrono::steady_clock::time_point m_lastGitCheck;
  // For the worker threads, see AsyncGuard.hpp.
  AliveFlag m_alive{MakeAliveFlag()};
  SessionStatus m_status = SessionStatus::Starting;
  // The session waits for its machine (StartProgram()) to be ready.
  bool m_waitingForTmux{false};
  // The next question about the machine asks it again, also when it is known
  // (Restart()): tmux may have been installed, or the host may be there now.
  bool m_askMachineAgain{false};
  // The message bar and what it has (see ShowInfo()).
  wxInfoBar *m_infoBar{nullptr};
  // The version of the tmux of the machine, once it is ready: the session
  // starts in tmux. Asked again at each Restart().
  std::optional<tmux::Version> m_tmuxVersion;
  // What the strip at the bottom of the terminal says instead of the header,
  // see SetNote(). Ends at the next Restart().
  wxString m_note;
  // Counts the questions of CheckTmuxSessionAfterTerminated(); Restart() makes
  // the answer of an earlier one stale.
  int m_endedCheck{0};
  // The program of the session was typed into the terminal. Until then,
  // SendCommands() keeps its commands in m_commandsAfterStart.
  bool m_programStarted{false};
  std::vector<wxString> m_commandsAfterStart;
  ProgramActivity m_programActivity{ProgramActivity::None};
  ProgramActivity m_terminalActivity{ProgramActivity::None};
  ProgramActivity m_reviewActivity{ProgramActivity::None};
  wxString m_reviewerName;      // The agent of the review buddy's terminal
  bool m_reviewerWorking{true}; // Reviewing, as opposed to waiting
  wxString m_defaultTitle;      // The window title
  wxString m_terminalTitle;     // The title the terminal set; in the status bar
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor{nullptr};
};

wxDECLARE_EVENT(wxEVT_SESSION_EXITED, wxCommandEvent);
// The user closed a session that could not start (its machine has no tmux, or
// no tmux-shim): GetString() is its name. The notebook ends its tmux session
// and closes the tab. Unlike an exit, which only closes the tab.
wxDECLARE_EVENT(wxEVT_SESSION_CLOSE_REQUESTED, wxCommandEvent);
// The folder of a page changed. The event object is the page.
wxDECLARE_EVENT(wxEVT_SESSION_CWD, wxCommandEvent);
// A field of the session that is saved in the workspace changed (its tmux
// name): it is saved again. The event object is the page.
wxDECLARE_EVENT(wxEVT_SESSION_CHANGED, wxCommandEvent);
// The program activity of a page changed (see SessionPage::ProgramActivity).
// The event object is the page; read the state from it.
wxDECLARE_EVENT(wxEVT_SESSION_PROGRAM, wxCommandEvent);
#endif // SESSIONPAGE_HPP
