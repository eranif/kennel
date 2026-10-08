#include "SessionPage.hpp"

#include "MainFrame.h"
#include "ThemeManager.h"
#include "app/ReviewBuddy.hpp"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "core/SftpClient.h"
#include "core/WslPath.h"

#include "terminal_event.h"
#include "terminal_view.h"

#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/infobar.h>
#include <wx/menu.h>
#include <wx/splitter.h>
#include <wx/utils.h>
#include <wx/xrc/xmlres.h>

#include <thread>

#include <cstring>
#include <map>
#include <optional>
#include <wx/choicdlg.h>
#include <wx/msgdlg.h>

wxDEFINE_EVENT(wxEVT_SESSION_IDLE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_ACTIVE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_EXITED, wxCommandEvent);

namespace {

// Menu ids of the review buddy entries. Fixed, because ids from
// wxWindow::NewControlId() would run out after enough right-clicks.
const int kIdResend = XRCID("review-buddy-resend");
const int kIdStop = XRCID("review-buddy-stop");
const int kIdOpen = XRCID("review-buddy-open");
const int kIdClose = XRCID("review-buddy-close");

// Text we can show in the built-in editor: not huge and no NUL bytes in the
// first chunk (the same heuristic git uses to spot binary files).
bool LooksLikeTextFile(const wxString &path) {
  constexpr wxFileOffset kMaxEditableSize = 16 * 1024 * 1024;
  wxFile file(path);
  if (!file.IsOpened() || file.Length() > kMaxEditableSize) {
    return false;
  }
  char head[8192];
  const ssize_t n = file.Read(head, sizeof(head));
  if (n < 0) {
    return false;
  }
  return std::memchr(head, 0, static_cast<size_t>(n)) == nullptr;
}

std::optional<wxTerminalViewCtrl::EnvironmentList>
BuildEnvironment(const std::map<wxString, wxString> &overrides) {
  if (overrides.empty()) {
    return std::nullopt;
  }

  wxEnvVariableHashMap env;
  wxGetEnvMap(&env);
  for (const auto &[key, value] : overrides) {
    env[key] = value;
  }

  wxTerminalViewCtrl::EnvironmentList list;
  list.reserve(env.size());
  for (const auto &entry : env) {
    wxString line;
    line << entry.first << "=" << entry.second;
    list.push_back(line.ToStdString(wxConvUTF8));
  }
  return list;
}
} // namespace

SessionPage::SessionPage(wxBookCtrlBase *parent, std::optional<AgentDef> agent,
                         Session session, bool resume)
    : SessionBasePage(parent), m_paths(AppManager::Get().Paths()),
      m_agent(std::move(agent)), m_session(std::move(session)),
      m_resume(resume) {
  SetDefaultSessionName(m_session.name);
  m_infoBar = new wxInfoBar(this);
  GetSizer()->Add(m_infoBar, wxSizerFlags().Expand());
  CreateTerminal();
}

SessionPage::~SessionPage() { m_alive->store(false); }

bool SessionPage::IsActive() const {
  return GetMainFrame()->IsWindowActive(this);
}

void SessionPage::SetDefaultSessionName(const wxString &name) {
  m_defaultTitle = MakeAppTitle(name, m_session.agentName);
  m_session.name = name;
  ApplyTitle();
}

void SessionPage::CreateTerminal() {
  std::vector<wxString> commands;
  std::optional<wxTerminalViewCtrl::EnvironmentList> env{std::nullopt};
  if (m_session.IsJobRun()) {
    commands = m_session.jobCommands;
    if (m_agent) {
      env = BuildEnvironment(m_agent->env);
    }
  } else if (m_agent) {
    commands = BuildCommandLine(*m_agent, m_session.workingDir, m_resume);
    env = BuildEnvironment(m_agent->env);
  }

  std::optional<wxString> cwd;
  if (!m_session.workingDir.empty()) {
    cwd = m_session.workingDir;
  }

  const auto &prefs = AppManager::Get().GetPrefs();
  // Use the default login shell
  wxString shellCommand = prefs.terminalLoginShell;

  if (m_session.plainTerminal) {
    // For plain terminals, we use the following logic to determine the
    // terminal: Assume the default terminal. If we have multiple terminals,
    // prompt the user to pick one (we set the default terminal as the default
    // terminal in the selection dialog).
    const auto &shells = ::FindShells().shells;
    if (shells.size() > 1) {
      wxArrayString shell_names_arr;
      std::unordered_map<wxString, wxString> m;
      wxString selectedShellName;
      for (const auto &[shell_name, shell_cmd] : shells) {
        if (shell_cmd == prefs.terminalLoginShell) {
          selectedShellName = shell_name;
        }
        shell_names_arr.push_back(shell_name);
        m.insert({shell_name, shell_cmd});
      }

      int selection = shell_names_arr.Index(selectedShellName);
      wxString selectionShell = ::wxGetSingleChoice(
          _("Choose a Login Shell:"), "Kennel", shell_names_arr, selection);
      if (selectionShell.empty()) {
        return; // user cancelled
      }
      shellCommand = m[selectionShell];
    }
  }

  if (m_agent) {
    if (!m_agent->loginShell.empty()) {
      // This agent has a custom shell -> use it
      shellCommand = m_agent->loginShell;
    }
    if (!m_agent->remoteHost.empty()) {
      KLOG_INFO() << "Launching remote session: " << m_session.name;
      for (const auto &cmd : commands) {
        KLOG_INFO() << " > " << cmd;
      }
      cwd.reset();
      env.reset();
    } else {
      if (!m_session.workingDir.empty()) {
        wxFileName::Mkdir(m_session.workingDir, wxS_DIR_DEFAULT,
                          wxPATH_MKDIR_FULL);
      }
      KLOG_INFO() << "Launching local session '" << m_session.name
                  << "': " << commands[0]
                  << (cwd ? wxString(" (cwd=" + *cwd + ")") : wxString());
    }
  }

  m_monitor = std::make_unique<ActivityMonitor>(
      [this]() { SetStatus(SessionStatus::Running); },
      [this]() { SetStatus(SessionStatus::Idle); });

  shellCommand.Replace(
      "%WORKING_DIRECTORY%",
      (m_session.workingDir.empty() ? "~" : m_session.workingDir));

  KLOG_INFO() << "Running shell: " << shellCommand;
  if (m_splitter == nullptr) {
    m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition,
                                      wxDefaultSize, wxSP_LIVE_UPDATE);
    m_splitter->SetSashGravity(0.5);
    m_splitter->SetMinimumPaneSize(150);
    GetSizer()->Add(m_splitter, wxSizerFlags(1).Expand());
  }
  m_mainPane = NewTerminalPane();
  m_terminal = new wxTerminalViewCtrl(m_mainPane, shellCommand, env, cwd);
  AddToPane(m_mainPane, m_terminal);
  m_splitter->Initialize(m_mainPane);
  m_acceleratorInterceptor =
      std::make_unique<AcceleratorInterceptor>(m_terminal);
  GetSizer()->Layout();
  for (const auto &cmd : commands) {
    m_terminal->SendCommand(cmd);
  }

  m_terminal->SetOutputCallback(
      [this](const std::string &chunk) { m_monitor->OnOutput(chunk); });
  m_terminal->Bind(wxEVT_TERMINAL_TERMINATED, &SessionPage::OnTerminated, this);
  m_terminal->Bind(wxEVT_TERMINAL_TITLE_CHANGED, &SessionPage::OnTitleChanged,
                   this);

  ConfigureTerminal(m_terminal);
  m_terminal->EnsureStarted();
  SetStatus(SessionStatus::Running);
  CheckGitRepo();
}

wxPanel *SessionPage::NewTerminalPane() {
  // wxBORDER_NONE without wxTAB_TRAVERSAL: the terminal takes the Tab key.
  auto *pane = new wxPanel(m_splitter, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxBORDER_NONE);
  pane->SetSizer(new wxBoxSizer(wxVERTICAL));
  return pane;
}

void SessionPage::FocusTerminal(wxTerminalViewCtrl *terminal) {
  if (terminal == nullptr || !IsActive() || !IsShownOnScreen()) {
    return;
  }
  wxWindow *focused = wxWindow::FindFocus();
  if (focused != nullptr && focused != this && !IsDescendant(focused)) {
    return;
  }
  // The user is typing in the other terminal of this page: moving the focus
  // would send their next keys to the other agent. The request was typed into
  // `terminal` anyway; the user switches when they are ready.
  constexpr auto kTypingPause = std::chrono::seconds(3);
  if (m_lastKeyTerminal != nullptr && m_lastKeyTerminal != terminal &&
      std::chrono::steady_clock::now() - m_lastKeyTime < kTypingPause) {
    return;
  }
  // Also when the application window is not active: the focus is restored to
  // this terminal when it is.
  m_lastFocused = terminal;
  terminal->SetFocus();
}

void SessionPage::AddToPane(wxPanel *pane, wxTerminalViewCtrl *terminal) {
  // A click on the border around a terminal focuses it too.
  pane->Bind(wxEVT_LEFT_DOWN, [terminal](wxMouseEvent &event) {
    event.Skip();
    terminal->SetFocus();
  });
  pane->GetSizer()->Add(terminal, wxSizerFlags(1).Expand().Border(
                                      wxLEFT | wxRIGHT, pane->FromDIP(5)));
}

void SessionPage::ConfigureTerminal(wxTerminalViewCtrl *terminal) {
  const auto &prefs = AppManager::Get().GetPrefs();
  terminal->EnableSafeDrawing(!prefs.terminalOptimizedDrawing);
  if (auto theme = ThemeManager::Get().ActiveTheme()) {
    terminal->SetTheme(*theme);
  }
  terminal->SetBufferSize(prefs.scrollbackLines);
  terminal->Bind(wxEVT_TERMINAL_TEXT_LINK, &SessionPage::OnTerminalLink, this);
  // Bound after the terminal's own handler, so it runs first; it does not call
  // Skip(), which keeps the terminal's Copy / Paste / Clear menu from showing.
  terminal->Bind(wxEVT_CONTEXT_MENU, &SessionPage::OnTerminalContextMenu, this);
  // The terminal takes the focus when the mouse goes down in it (it does so
  // on release too), so the one the user clicks is the one that gets the keys.
  terminal->Bind(wxEVT_LEFT_DOWN, [terminal](wxMouseEvent &event) {
    event.Skip();
    if (!terminal->HasFocus()) {
      terminal->SetFocus();
    }
  });
  // Remember who is typing; this runs before the terminal's own handler.
  terminal->Bind(wxEVT_CHAR_HOOK, [this, terminal](wxKeyEvent &event) {
    m_lastKeyTerminal = terminal;
    m_lastKeyTime = std::chrono::steady_clock::now();
    event.Skip();
  });
  terminal->Bind(wxEVT_SET_FOCUS, [this, terminal](wxFocusEvent &event) {
    m_lastFocused = terminal;
    event.Skip();
  });
}

std::vector<wxTerminalViewCtrl *> SessionPage::GetTerminals() const {
  std::vector<wxTerminalViewCtrl *> terminals;
  if (m_terminal != nullptr) {
    terminals.push_back(m_terminal);
  }
  if (m_reviewTerminal != nullptr) {
    terminals.push_back(m_reviewTerminal);
  }
  return terminals;
}

void SessionPage::SetFocus() {
  wxTerminalViewCtrl *target =
      m_lastFocused != nullptr && m_lastFocused == m_reviewTerminal
          ? m_reviewTerminal
          : m_terminal;
  if (target != nullptr) {
    target->SetFocus();
  } else {
    SessionBasePage::SetFocus();
  }
}

void SessionPage::Restart() {
  KLOG_INFO() << "Restarting session '" << m_session.name << "'";
  CloseReviewBuddy();
  if (m_splitter != nullptr) {
    GetSizer()->Detach(m_splitter);
    m_splitter->Destroy(); // and the panes, with the terminal in it
    m_splitter = nullptr;
    m_mainPane = nullptr;
    m_terminal = nullptr;
    m_lastFocused = nullptr;
    m_lastKeyTerminal = nullptr;
    m_acceleratorInterceptor.reset();
  }
  m_status = SessionStatus::Starting;
  m_resume = true;
  CallAfter(&SessionPage::CreateTerminal);
}

void SessionPage::OnTerminated(wxTerminalEvent &evt) {
  evt.Skip();
  SetStatus(SessionStatus::Exited);
}

void SessionPage::OnTitleChanged(wxTerminalEvent &evt) {
  evt.Skip();
  m_terminalTitle = MakeAppTitle(evt.GetTitle(), m_session.agentName);
  ApplyTitle();
}

void SessionPage::SetStatus(SessionStatus status) {
  if (m_status == status) {
    return;
  }
  m_status = status;
  switch (m_status) {
  case SessionStatus::Exited: {
    wxCommandEvent e{wxEVT_SESSION_EXITED};
    e.SetString(m_session.name);
    GetParent()->GetEventHandler()->AddPendingEvent(e);
    break;
  }
  case SessionStatus::Running: {
    wxCommandEvent e{wxEVT_SESSION_ACTIVE};
    e.SetString(m_session.name);
    GetParent()->GetEventHandler()->AddPendingEvent(e);
    break;
  }
  case SessionStatus::Idle: {
    wxCommandEvent e{wxEVT_SESSION_IDLE};
    e.SetString(m_session.name);
    GetParent()->GetEventHandler()->AddPendingEvent(e);
    break;
  }
  default:
    break;
  }
}

void SessionPage::OnTerminalLink(wxTerminalEvent &evt) {
  wxString text = evt.GetClickedText();
  wxString textLower = text.Lower();

  if (textLower.StartsWith("http://") || textLower.StartsWith("https://")) {
    ::wxLaunchDefaultBrowser(text);
    return;
  }

  if (m_agent && m_agent->IsRemote()) {
    // The path lives on the remote host: fetch it over SFTP.
    std::vector<wxString> searchDirs;
    if (!m_session.workingDir.empty()) {
      searchDirs.push_back(m_session.workingDir);
    }
    searchDirs.push_back("$HOME/.kennel/sessions");
    GetMainFrame()->GetMainView()->OpenRemoteFile(
        RemoteHostDetails{m_agent->remoteHost, m_agent->remoteUser}, text,
        searchDirs);
    return;
  }

  wxFileName fn;
  if (m_agent && m_agent->IsWSL()) {
    // The path is a Linux path inside the distro: reach it through the
    // distro's file share.
    const wxString distro = wsl::DistroOf(m_agent->loginShell);
    const wxString linuxPath =
        wsl::ResolveLinuxPath(distro, text, m_session.workingDir);
    if (linuxPath.empty()) {
      return;
    }
    fn = wxFileName(wsl::ToWindowsPath(distro, linuxPath));
  } else {
    if (text == "~" || text.StartsWith("~/")) {
      text = wxGetHomeDir() + text.Mid(1);
    }

    // Terminal output is usually relative to the shell's cwd, not Kennel's own
    // process cwd, so resolve against the session's launch directory before
    // checking for existence.
    fn = wxFileName{text};
    if (!fn.IsAbsolute()) {
      fn.MakeAbsolute(m_session.workingDir.empty() ? wxGetHomeDir()
                                                   : m_session.workingDir);
    }
  }
  if (!fn.FileExists()) {
    return;
  }
  const wxString fullPath = fn.GetFullPath();
  if (!LooksLikeTextFile(fullPath)) {
    // Images, PDFs, archives, ...: the OS knows best.
    ::wxLaunchDefaultApplication(fullPath);
    return;
  }
  // Deferred: don't rebuild the tree from inside the terminal's mouse handler.
  GetMainFrame()->GetMainView()->CallAfter(&MainView::OpenLocalFile, fullPath);
}

void SessionPage::ApplyTheme(const wxTerminalTheme &theme) {
  for (auto *terminal : GetTerminals()) {
    terminal->SetTheme(theme);
  }
  // The border around each terminal has the theme's background.
  for (wxPanel *pane : {m_mainPane, m_reviewPane}) {
    if (pane != nullptr) {
      pane->SetBackgroundColour(theme.bg);
      pane->Refresh();
    }
  }
}

void SessionPage::ApplyTitle() {
  if (IsActive() && IsShown()) {
    auto *frame = dynamic_cast<wxFrame *>(wxTheApp->GetTopWindow());
    frame->SetLabel(m_terminalTitle.empty() ? m_defaultTitle : m_terminalTitle);
  }
}

// ---------------------------------------------------------------------------
// Context menu and review buddy
// ---------------------------------------------------------------------------

void SessionPage::OnTerminalContextMenu(wxContextMenuEvent &evt) {
  if (auto *terminal =
          dynamic_cast<wxTerminalViewCtrl *>(evt.GetEventObject())) {
    ShowTerminalMenu(terminal);
  }
}

void SessionPage::ShowTerminalMenu(wxTerminalViewCtrl *terminal) {
  wxMenu menu;
  if (terminal->CanCopy()) {
    menu.Append(wxID_COPY, _("Copy"));
    menu.Bind(
        wxEVT_MENU, [terminal](wxCommandEvent &) { terminal->Copy(); },
        wxID_COPY);
  }
  menu.Append(wxID_PASTE, _("Paste"));
  menu.Bind(
      wxEVT_MENU, [terminal](wxCommandEvent &) { terminal->Paste(); },
      wxID_PASTE);
  menu.AppendSeparator();
  menu.Append(wxID_CLEAR, _("Clear buffer"));
  menu.Bind(
      wxEVT_MENU, [terminal](wxCommandEvent &) { terminal->ClearAll(); },
      wxID_CLEAR);

  if (m_review) {
    menu.AppendSeparator();
    menu.Append(wxID_ANY, _("Review Buddy: ") + m_review->Describe())
        ->Enable(false);
    if (m_review->IsRunning() || m_review->HasStalled()) {
      menu.Append(kIdResend, _("Send the Request Again"))
          ->Enable(!m_review->IsBusy());
      menu.Bind(
          wxEVT_MENU, [this](wxCommandEvent &) { m_review->Resend(); },
          kIdResend);
      menu.Append(kIdStop, _("Stop the Review"));
      menu.Bind(
          wxEVT_MENU, [this](wxCommandEvent &) { m_review->Stop(); }, kIdStop);
    }
    menu.Append(kIdOpen, _("Open the Latest Review"))
        ->Enable(!m_review->CommentsPath().empty());
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { OpenLatestReview(); }, kIdOpen);
    menu.Append(kIdClose, _("Close Review Buddy"));
    menu.Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) { CallAfter(&SessionPage::CloseReviewBuddy); },
        kIdClose);
  } else if (terminal == m_terminal && m_agent && !m_session.IsJobRun()) {
    menu.AppendSeparator();
    RefreshGitState();
    wxString whyNot;
    const auto candidates = GetReviewerCandidates();
    if (!CanHaveReviewBuddy(whyNot)) {
      menu.Append(wxID_ANY, _("Launch Review Buddy") + " (" + whyNot + ")")
          ->Enable(false);
    } else if (candidates.empty()) {
      menu.Append(wxID_ANY, _("Launch Review Buddy (no agent on this host)"))
          ->Enable(false);
    } else {
      auto *submenu = new wxMenu();
      std::map<int, AgentDef> byId;
      int id = wxID_HIGHEST + 1;
      for (const AgentDef &candidate : candidates) {
        submenu->Append(id, candidate.name);
        byId.emplace(id++, candidate);
      }
      // Bound on the submenu: its items send their events there first.
      submenu->Bind(wxEVT_MENU, [this, byId](wxCommandEvent &event) {
        auto it = byId.find(event.GetId());
        if (it != byId.end()) {
          // Deferred: leave the menu's callback first.
          CallAfter(&SessionPage::LaunchReviewBuddy, it->second);
        }
      });
      menu.AppendSubMenu(submenu, _("Launch Review Buddy"));
    }
  }

  terminal->PopupMenu(&menu);
}

std::vector<AgentDef> SessionPage::GetReviewerCandidates() const {
  std::vector<AgentDef> result;
  if (!m_agent) {
    return result;
  }
  // Both agents must see the same folder, so they must run on the same host,
  // and in the same WSL distro if that is where the agent runs.
  const wxString distro = wsl::DistroOf(m_agent->loginShell);
  for (const AgentDef &agent : AppManager::Get().Adapters().Agents()) {
    if (agent.remoteHost == m_agent->remoteHost &&
        agent.remoteUser == m_agent->remoteUser &&
        wsl::DistroOf(agent.loginShell) == distro) {
      result.push_back(agent);
    }
  }
  return result;
}

bool SessionPage::CanHaveReviewBuddy(wxString &whyNot) {
  if (m_session.workingDir.empty() && !(m_agent && m_agent->IsRemote())) {
    whyNot = _("no working directory");
    return false;
  }
  switch (m_gitState) {
  case GitState::Yes:
    return true;
  case GitState::No:
    whyNot = _("not a git repository");
    return false;
  case GitState::Unknown:
    whyNot = _("looking for a git repository");
    return false;
  }
  return false;
}

void SessionPage::RefreshGitState() {
  // `git init` may have run in the session since the last look. A local check
  // is one stat. A remote one opens an SSH connection, so it is repeated at
  // most every 30 seconds (a check that failed is not "No", it is "Unknown",
  // and is repeated at once).
  constexpr auto kRecheckAfter = std::chrono::seconds(30);
  const bool stale =
      std::chrono::steady_clock::now() - m_lastGitCheck > kRecheckAfter;
  const bool remote = m_agent && m_agent->IsRemote();
  if (m_gitState == GitState::Unknown ||
      (m_gitState == GitState::No && (!remote || stale))) {
    CheckGitRepo();
  }
}

void SessionPage::CheckGitRepo() {
  if (!m_agent || m_session.IsJobRun() || m_gitCheckBusy) {
    return;
  }
  m_lastGitCheck = std::chrono::steady_clock::now();
  const wxString dir = m_session.workingDir;
  if (m_agent->IsWSL()) {
    CheckWslGitRepo();
    return;
  }
  if (!m_agent->IsRemote()) {
    // A worktree or a submodule has a .git file instead of a folder.
    const wxString git = dir + "/.git";
    m_gitState = !dir.empty() && (wxDir::Exists(git) || wxFile::Exists(git))
                     ? GitState::Yes
                     : GitState::No;
    return;
  }

  m_gitCheckBusy = true;
  const wxString host = m_agent->remoteHost;
  const wxString user = m_agent->remoteUser;
  const wxString path = (dir.empty() ? wxString("~") : dir) + "/.git";
  std::thread([this, alive = m_alive, host, user, path] {
    auto result = SftpClient::Exists(host, user, path);
    const bool ok = result.ok();
    const bool exists = ok && result.value();
    CallAfterIfAlive(alive, [this, ok, exists] {
      m_gitCheckBusy = false;
      m_gitState =
          !ok ? GitState::Unknown : (exists ? GitState::Yes : GitState::No);
    });
  }).detach();
}

void SessionPage::CheckWslGitRepo() {
  if (m_session.workingDir.empty()) {
    m_gitState = GitState::No;
    return;
  }
  // The distro's file share can be slow to answer (or have to start the
  // distro), so look in the background, like for a remote host.
  m_gitCheckBusy = true;
  const wxString distro = wsl::DistroOf(m_agent->loginShell);
  const wxString dir = m_session.workingDir;
  std::thread([this, alive = m_alive, distro, dir] {
    const wxString linuxDir = wsl::ResolveLinuxPath(distro, dir, wxEmptyString);
    GitState state = GitState::Unknown;
    if (!linuxDir.empty() && wxDir::Exists(wsl::ToWindowsPath(distro, "/"))) {
      // A worktree or a submodule has a .git file instead of a folder.
      const wxString git = wsl::ToWindowsPath(distro, linuxDir + "/.git");
      state = wxDir::Exists(git) || wxFile::Exists(git) ? GitState::Yes
                                                        : GitState::No;
    }
    CallAfterIfAlive(alive, [this, state] {
      m_gitCheckBusy = false;
      m_gitState = state;
    });
  }).detach();
}

wxString SessionPage::HostWorkingDir() const {
  if (m_agent && m_agent->IsWSL()) {
    const wxString distro = wsl::DistroOf(m_agent->loginShell);
    const wxString linuxDir =
        wsl::ResolveLinuxPath(distro, m_session.workingDir, wxEmptyString);
    return linuxDir.empty() ? wxString{} : wsl::ToWindowsPath(distro, linuxDir);
  }
  return m_session.workingDir;
}

void SessionPage::LaunchReviewBuddy(const AgentDef &reviewer) {
  if (m_review || m_reviewTerminal != nullptr || m_terminal == nullptr ||
      !m_agent) {
    return;
  }
  KLOG_INFO() << "Launching review buddy '" << reviewer.name << "' for '"
              << m_session.name << "'";

  // The folder as Kennel reaches it (it reads and writes the review files).
  const wxString hostDir = HostWorkingDir();
  if (hostDir.empty() && !m_agent->IsRemote()) {
    m_infoBar->ShowMessage(_("Cannot find the session's working directory"),
                           wxICON_WARNING);
    return;
  }

  // The reviewer's pane opens when the first request is ready (it is written
  // first, possibly over SSH): the agent reads it as soon as it starts.
  DismissNotice(); // The message about an earlier review
  m_review = std::make_unique<ReviewBuddy>(
      ReviewBuddy::Target{hostDir, m_agent->remoteHost, m_agent->remoteUser,
                          m_session.name},
      m_terminal,
      [this, reviewer](const wxString &prompt) {
        return StartReviewer(reviewer, prompt);
      },
      // Whether the user is looking at this session right now.
      [this] { return IsActive() && IsShownOnScreen(); },
      [this](const wxString &message, bool problem) {
        m_infoBar->ShowMessage(message,
                               problem ? wxICON_WARNING : wxICON_INFORMATION);
      },
      [this](wxTerminalViewCtrl *terminal) { FocusTerminal(terminal); });
  m_review->Begin();
}

wxTerminalViewCtrl *SessionPage::StartReviewer(const AgentDef &reviewer,
                                               const wxString &prompt) {
  if (m_reviewTerminal != nullptr || m_terminal == nullptr) {
    return m_reviewTerminal;
  }

  const auto &prefs = AppManager::Get().GetPrefs();
  wxString shellCommand = prefs.terminalLoginShell;
  if (!reviewer.loginShell.empty()) {
    shellCommand = reviewer.loginShell;
  }
  // A WSL agent's directory is a Linux path: the shell command takes it.
  shellCommand.Replace(
      "%WORKING_DIRECTORY%",
      (m_session.workingDir.empty() ? "~" : m_session.workingDir));
  std::optional<wxString> cwd;
  if (!reviewer.IsRemote() && !reviewer.IsWSL() &&
      !m_session.workingDir.empty()) {
    cwd = m_session.workingDir;
  }

  // As for the agent's own terminal (CreateTerminal), the environment is the
  // one of the local process: the shell, or the ssh client for a remote agent.
  // BuildCommandLine() exports the same variables on the remote side.
  m_reviewPane = NewTerminalPane();
  m_reviewTerminal = new wxTerminalViewCtrl(
      m_reviewPane, shellCommand, BuildEnvironment(reviewer.env), cwd);
  AddToPane(m_reviewPane, m_reviewTerminal);
  m_reviewAcceleratorInterceptor =
      std::make_unique<AcceleratorInterceptor>(m_reviewTerminal);
  for (const wxString &command :
       BuildCommandLine(reviewer, m_session.workingDir, false, prompt)) {
    m_reviewTerminal->SendCommand(command);
  }
  ConfigureTerminal(m_reviewTerminal);
  m_reviewTerminal->Bind(wxEVT_TERMINAL_TERMINATED, [this](wxTerminalEvent &e) {
    e.Skip();
    // The reviewer quit: close its pane, as a session's tab closes when its
    // process exits.
    CallAfter(&SessionPage::CloseReviewBuddy);
  });

  m_splitter->SplitVertically(m_mainPane, m_reviewPane);
  m_reviewTerminal->EnsureStarted();
  // The reviewer is the active agent now; it may ask for a permission.
  FocusTerminal(m_reviewTerminal);
  return m_reviewTerminal;
}

void SessionPage::DismissNotice() {
  if (m_infoBar != nullptr && m_infoBar->IsShown()) {
    m_infoBar->Dismiss();
  }
}

void SessionPage::CloseReviewBuddy() {
  m_review.reset(); // stops its timers
  DismissNotice();
  if (m_reviewTerminal == nullptr) {
    return;
  }
  if (m_lastFocused == m_reviewTerminal) {
    m_lastFocused = m_terminal;
  }
  if (m_lastKeyTerminal == m_reviewTerminal) {
    m_lastKeyTerminal = nullptr;
  }
  if (m_splitter != nullptr) {
    m_splitter->Unsplit(m_reviewPane);
  }
  m_reviewPane->Destroy(); // and the terminal in it
  m_reviewPane = nullptr;
  m_reviewTerminal = nullptr;
  m_reviewAcceleratorInterceptor.reset();
  if (m_terminal != nullptr) {
    m_terminal->SetFocus();
  }
}

void SessionPage::OpenLatestReview() {
  CHECK_NOT_NULL_RETURN(m_review);
  const wxString relative = m_review->CommentsPath();
  CHECK_NOT_EMPTY_OR_RETURN(relative);

  auto *mainView = GetMainFrame()->GetMainView();
  const wxString &dir = m_session.workingDir;
  if (m_agent && m_agent->IsRemote()) {
    std::vector<wxString> searchDirs;
    if (!dir.empty()) {
      searchDirs.push_back(dir);
    }
    mainView->OpenRemoteFile(
        RemoteHostDetails{m_agent->remoteHost, m_agent->remoteUser}, relative,
        searchDirs);
    return;
  }
  const wxString hostDir = HostWorkingDir();
  CHECK_NOT_EMPTY_OR_RETURN(hostDir);
  mainView->OpenLocalFile(wxFileName(hostDir, relative).GetFullPath());
}
