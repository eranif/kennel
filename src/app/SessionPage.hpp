#ifndef SESSIONPAGE_HPP
#define SESSIONPAGE_HPP
#include "UI.hpp"
#include "app/AcceleratorInterceptor.h"
#include "app/AsyncGuard.hpp"
#include "app/wxCustomStatusBar.h"
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

class wxActivityIndicator;
class wxTerminalViewCtrl;
class wxTerminalEvent;
class wxSplitterWindow;
class wxPanel;
class wxContextMenuEvent;
class ReviewBuddy;

enum class SessionStatus { Starting, Running, Idle, Exited, Error };

class SessionPage : public SessionBasePage {
public:
  using StatusChangedFn = std::function<void(SessionStatus)>;

  SessionPage(wxBookCtrlBase *parent, std::optional<AgentDef> agent,
              Session session, bool resume = false);
  ~SessionPage() override;

  SessionStatus Status() const { return m_status; }
  const Session &GetSession() const { return m_session; }
  Session &GetSession() { return m_session; }
  inline bool IsPlainTerminal() const { return !m_agent.has_value(); }
  void Restart();
  // The agent's own terminal.
  wxTerminalViewCtrl *GetTerminal() { return m_terminal; }
  // The agent's terminal, plus the review buddy's while it is open.
  std::vector<wxTerminalViewCtrl *> GetTerminals() const;
  void ApplyTheme(const wxTerminalTheme &theme);
  // Focuses the terminal that had the focus last (the agent's by default).
  void SetFocus() override;

  bool IsActive() const;
  void SetDefaultSessionName(const wxString &name);
  void ApplyTitle();

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
  // The bar at the top of the page: session + agent (with the agent's icon),
  // where the agent runs. The main text area, first, shows the review state.
  void CreateStatusBar();
  void UpdateSessionField();
  // Sets the widths of the fields from the width of the page (cheap).
  void LayoutStatusBar();
  // Fills the main text area, and starts / stops the activity indicator:
  //   - while a review is in progress: the review state (the terminal title is
  //     not shown, but is remembered);
  //   - for a while after it ended, or after a problem: the notice;
  //   - otherwise: the terminal title.
  void UpdateMainText();
  // Shows `text` in the main text area for a while (see UpdateMainText()).
  void ShowNotice(const wxString &text);
  void ClearNotice();
  void SetBusy(bool busy);
  void OnReviewChanged(wxCommandEvent &) { UpdateMainText(); }
  void OnNoticeTimer(wxTimerEvent &) { UpdateMainText(); }
  void OnSize(wxSizeEvent &event);
  void OnTerminated(wxTerminalEvent &evt);
  void OnTitleChanged(wxTerminalEvent &evt);
  void OnTerminalLink(wxTerminalEvent &evt);

  // ---- Context menu and review buddy -----------------------------------
  // Replaces the terminal's own Copy / Paste / Clear menu.
  void OnTerminalContextMenu(wxContextMenuEvent &evt);
  void ShowTerminalMenu(wxTerminalViewCtrl *terminal);
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
  wxTerminalViewCtrl *StartReviewer(const AgentDef &reviewer,
                                    const wxString &prompt);
  void CloseReviewBuddy();
  void OpenLatestReview();
  wxBookCtrlBase *GetBook() const {
    return dynamic_cast<wxBookCtrlBase *>(GetParent());
  }

  AppPaths m_paths;
  std::optional<AgentDef> m_agent{std::nullopt};
  Session m_session;
  bool m_resume = false;

  enum class GitState { Unknown, Yes, No };

  wxCustomStatusBar *m_statusBar{nullptr};
  std::shared_ptr<wxCustomStatusBarBitmapField> m_sessionField;
  int m_sessionFieldBestWidth{0};            // in pixels
  wxActivityIndicator *m_indicator{nullptr}; // While a review is in progress
  std::shared_ptr<wxCustomStatusBarControlField> m_indicatorField;
  // What the main text area shows when there is no review in progress: the
  // notice while its timer runs, then the terminal title.
  wxString m_notice;
  wxTimer m_noticeTimer{this};
  bool m_endNoticeShown{false}; // For the review that ended

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
  GitState m_gitState{GitState::Unknown};
  bool m_gitCheckBusy{false};
  std::chrono::steady_clock::time_point m_lastGitCheck;
  // For the worker threads, see AsyncGuard.hpp.
  AliveFlag m_alive{MakeAliveFlag()};
  std::unique_ptr<ActivityMonitor> m_monitor;
  SessionStatus m_status = SessionStatus::Starting;
  wxString m_defaultTitle;  // The window title
  wxString m_terminalTitle; // The title the terminal set; in the status bar
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor{nullptr};
};

wxDECLARE_EVENT(wxEVT_SESSION_IDLE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SESSION_ACTIVE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SESSION_EXITED, wxCommandEvent);
#endif // SESSIONPAGE_HPP
