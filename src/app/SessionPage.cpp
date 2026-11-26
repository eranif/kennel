#include "SessionPage.hpp"
#include "ToolBarArt.h"
#include "app/ToolBarUtils.h"

#include "MainFrame.h"
#include "ThemeManager.h"
#include "app/AssetBootstrap.h"
#include "app/ReviewBuddy.hpp"
#include "app/ReviewBuddyHeader.hpp"
#include "app/SearchBar.hpp"
#include "app/TerminalManager.hpp"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/EventNotifier.hpp"
#include "core/FileLocation.h"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "core/PromptFileRemover.h"
#include "core/SftpClient.h"
#include "core/TmuxClient.h"
#include "core/TmuxShim.h"
#include "core/WslPath.h"

#include "terminal_event.h"
#include "terminal_view.h"

#include <wx/arrstr.h>
#include <wx/aui/auibar.h>
#include <wx/clipbrd.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/infobar.h>
#include <wx/menu.h>
#include <wx/splitter.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/xrc/xmlres.h>

#include <algorithm>
#include <thread>

#include <cstring>
#include <map>
#include <optional>
#include <wx/choicdlg.h>
#include <wx/msgdlg.h>

wxDEFINE_EVENT(wxEVT_SESSION_EXITED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_CLOSE_REQUESTED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_PROGRAM, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_CWD, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_CHANGED, wxCommandEvent);

namespace {

// How long the main text area shows how a review ended.
constexpr int kNoticeMs = 60 * 1000;

// Menu ids of the review buddy entries. Fixed, because ids from
// wxWindow::NewControlId() would run out after enough right-clicks.
const int kIdResend = XRCID("review-buddy-resend");
const int kIdStop = XRCID("review-buddy-stop");
const int kIdOpen = XRCID("review-buddy-open");
const int kIdClose = XRCID("review-buddy-close");

// Whether `path` is absolute on the machine Kennel runs on: "C:\x", "C:/x" or
// "\\server\share" on Windows, "/x" elsewhere. A "/x" path on Windows belongs
// to another machine (a Linux host reached with ssh, say).
bool IsAbsoluteLocalPath(const wxString &path) {
#ifdef __WXMSW__
  if (path.length() >= 3 && wxIsalpha(path[0]) && path[1] == ':' &&
      (path[2] == '\\' || path[2] == '/')) {
    return true;
  }
  return path.StartsWith("\\\\") || path.StartsWith("//");
#else
  return path.StartsWith("/");
#endif
}

// A path of an msys2 / Git Bash shell ("/c/Users/x") as a Windows path
// ("C:/Users/x"). Other paths are returned as they are. It assumes an msys
// shell: a Linux path of an ssh shell with a one-letter top folder ("/d/x") is
// mapped to the wrong drive.
wxString MsysToWindowsPath(const wxString &path) {
#ifdef __WXMSW__
  if (path.length() >= 2 && path[0] == '/' && wxIsalpha(path[1]) &&
      (path.length() == 2 || path[2] == '/')) {
    return wxString(static_cast<wxChar>(wxToupper(path[1]))) + ":" +
           (path.length() == 2 ? wxString("/") : path.Mid(2));
  }
#endif
  return path;
}

// `base` (absolute, see above) followed by `relative`, with "." and ".."
// resolved. Plain string work: nothing is looked up on disk.
wxString JoinLocalPath(const wxString &base, const wxString &relative) {
  wxString all = base + "/" + relative;
#ifdef __WXMSW__
  all.Replace("\\", "/");
  const bool unc = all.StartsWith("//");
  const size_t rootParts = unc ? 2 : 1; // "server/share" or "C:"
  const wxString prefix = unc ? "\\\\" : "";
  const wxString sep = "\\";
#else
  const size_t rootParts = 0;
  const wxString prefix = "/";
  const wxString sep = "/";
#endif
  std::vector<wxString> parts;
  for (const wxString &part : wxSplit(all, '/')) {
    if (part.empty() || part == ".") {
      continue;
    }
    if (part == ".." && parts.size() > rootParts) {
      parts.pop_back();
    } else if (part != "..") {
      parts.push_back(part);
    }
  }
  return prefix + wxJoin(parts, sep[0], '\0');
}

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

// A file or a folder of this machine, which a click on a path can open.
bool LocalPathExists(const wxString &path) {
  return ::wxFileExists(path) || ::wxDirExists(path);
}

// The file or folder of this machine that a click on `text` means, or empty.
// A relative `text` is looked for under each of `baseDirs`. `foreignShell` is
// set when `text` is a Linux path that this machine cannot reach (the shell
// runs on another host).
wxString ResolveLocalFile(const std::optional<AgentDef> &agent, wxString text,
                          std::vector<wxString> baseDirs, bool *foreignShell) {
  wxString fullPath;
  if (agent && agent->IsWSL()) {
    // The path is a Linux path inside the distro: reach it through the
    // distro's file share.
    const wxString distro = wsl::DistroOf(agent->loginShell);
    if (baseDirs.empty()) {
      baseDirs.push_back(wxEmptyString);
    }
    for (const wxString &base : baseDirs) {
      const wxString linuxPath = wsl::ResolveLinuxPath(distro, text, base);
      if (linuxPath.empty()) {
        continue;
      }
      const wxString winPath = wsl::ToWindowsPath(distro, linuxPath);
      if (LocalPathExists(winPath)) {
        fullPath = winPath;
        break;
      }
    }
  } else {
    if (text == "~" || text.StartsWith("~/")) {
      text = wxGetHomeDir() + text.Mid(1);
    }
    text = MsysToWindowsPath(text);
    if (IsAbsoluteLocalPath(text)) {
      if (LocalPathExists(text)) {
        fullPath = text;
      }
    } else if (text.StartsWith("/") || text.StartsWith("\\")) {
      // A Linux path on Windows (an ssh shell, say): not a file of this
      // machine.
      *foreignShell = true;
    } else {
      // Terminal output is relative to the shell's folder, not Kennel's own.
      // The home folder is the last resort, only when there is no other.
      if (baseDirs.empty()) {
        baseDirs.push_back(wxGetHomeDir());
      }
      for (const wxString &dir : baseDirs) {
        wxString base = MsysToWindowsPath(dir);
        if (base == "~" || base.StartsWith("~/")) {
          // The session's folder as the user typed it.
          base = wxGetHomeDir() + base.Mid(1);
        }
        if (!IsAbsoluteLocalPath(base)) {
          // A Linux folder on Windows: the shell is on a host Kennel cannot
          // reach as a file system (e.g. "ssh" from a Windows terminal into
          // Linux). Joining it with a local path would find the wrong file.
          // Any other relative folder is of no use either.
          *foreignShell |= base.StartsWith("/") || base.StartsWith("\\");
          continue;
        }
        const wxString path = JoinLocalPath(base, text);
        if (LocalPathExists(path)) {
          fullPath = path;
          break;
        }
      }
    }
  }
  return fullPath;
}

// The text of a terminal's header: "head | title | dir". The title is on one
// line; parts that are empty are left out.
wxString JoinHeader(wxString head, wxString title, const wxString &dir) {
  title.Replace("\n", " ");
  title.Trim().Trim(false);
  if (!title.empty()) {
    head << " | " << title;
  }
  if (!dir.empty()) {
    head << " | " << dir;
  }
  return head;
}
} // namespace

SessionPage::SessionPage(wxWindow *parent, std::optional<AgentDef> agent,
                         Session session, bool resume, SessionPage *owner)
    : SessionBasePage(parent), m_paths(AppManager::Get().Paths()),
      m_agent(std::move(agent)), m_session(std::move(session)),
      m_resume(resume), m_owner(owner) {
  SetDefaultSessionName(m_session.name);
  InitStatus();
  Bind(wxEVT_TIMER, &SessionPage::OnNoticeTimer, this, m_noticeTimer.GetId());
  // Every change of the status of the review buddy shows in the status bar.
  for (const auto &type :
       {wxEVT_REVIEW_BUDDY_STARTED, wxEVT_REVIEW_BUDDY_WAITING_FOR_FINDINGS,
        wxEVT_REVIEW_BUDDY_ADDRESSING_FINDINGS, wxEVT_REVIEW_BUDDY_FINISHED,
        wxEVT_REVIEW_BUDDY_ERROR, wxEVT_REVIEW_BUDDY_STOPPED}) {
    Bind(type, &SessionPage::OnReviewChanged, this);
  }
  // The sizer of the base page holds the splitters (wxCrafter): the toolbar
  // goes above them, the message bar below it, and the search bar at the end.
  // The review buddy's pane and the bottom pane are closed until they are used.
  CreateToolBar();
  wxSizer *outer = GetSizer();
  outer->Insert(0, m_toolbar, wxSizerFlags(0).Expand());
  // Hidden until there is something to say (ShowInfo()).
  m_infoBar = new wxInfoBar(this);
  outer->Insert(1, m_infoBar, wxSizerFlags(0).Expand());
  m_infoBar->Bind(wxEVT_BUTTON, &SessionPage::OnTmuxDownload, this,
                  ID_TMUX_DOWNLOAD);
  m_infoBar->Bind(wxEVT_BUTTON, &SessionPage::OnTmuxClose, this, ID_TMUX_CLOSE);
  m_infoBar->Bind(wxEVT_BUTTON, &SessionPage::OnHandoffCancel, this,
                  ID_HANDOFF_CANCEL);
  m_searchBar = new SearchBar(this, SearchBar::Kind::kFindOnly);
  m_searchBar->Hide();
  outer->Add(m_searchBar, wxSizerFlags(0).Expand());
  m_splitterReviewBuddy->Unsplit(m_reviewBuddyPanel);
  m_splitterMain->Unsplit(m_temrinalPanel);
  // A click on the border around a terminal focuses it too.
  m_agentPanel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
    event.Skip();
    if (m_terminal != nullptr) {
      m_terminal->SetFocus();
    }
  });
  m_reviewBuddyPanel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
    event.Skip();
    if (m_reviewTerminal != nullptr) {
      m_reviewTerminal->SetFocus();
    }
  });
  Bind(wxEVT_SEARCHBAR_FIND, &SessionPage::OnSearchBarFind, this);
  Bind(wxEVT_SEARCHBAR_CLOSE, &SessionPage::OnSearchBarClose, this);
  m_promptEnterTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &SessionPage::OnPromptEnterTimer, this,
       m_promptEnterTimer.GetId());
  EventNotifier::Get()->Bind(wxEVT_TMUX_MACHINE, &SessionPage::OnTmuxMachine,
                             this);
  Bind(wxEVT_REVIEW_BUDDY_CLOSE, [this](ReviewBuddyEvent &event) {
    // The buddy that asked may be closed already, and a new one started.
    if (m_review && m_review->LoopId() == event.GetString()) {
      CallAfter(&SessionPage::CloseReviewBuddy);
    }
  });
  CreateTerminal();
  StartProgram();
  if (m_session.terminalPane) {
    // A restored session has the terminal pane it had (in tmux, attached to its
    // tmux session), shown or hidden as it was. After the page is in the book.
    CallAfter([this] {
      if (CanToggleTerminalPane()) {
        SetTerminalPane(m_session.terminalPaneShown, /*focus=*/false);
      }
    });
  }
#ifndef __WXMAC__
  // Bind paint event for the splitters.
  for (auto *splitrer : {m_splitterMain, m_splitterReviewBuddy}) {
    splitrer->Bind(wxEVT_PAINT, [splitrer, this](wxPaintEvent &) {
      wxAutoBufferedPaintDC dc{splitrer};
      const auto bgColour = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);
      dc.SetPen(bgColour);
      dc.SetBrush(bgColour);
      dc.DrawRectangle(GetClientRect());
    });

    splitrer->Bind(wxEVT_SPLITTER_SASH_POS_CHANGED,
                   [this](wxSplitterEvent &event) {
                     event.Skip();
                     static_cast<wxWindow *>(event.GetEventObject())->Refresh();
                   });
  }
#endif
}

SessionPage *SessionPage::Clone(wxWindow *parent) {
  Session terminal;
  terminal.name = m_session.name;
  terminal.groupName = m_session.groupName;
  terminal.workingDir = m_session.workingDir;
  terminal.sideDir = m_session.sideDir;
  terminal.plainTerminal = true;
  terminal.agentName = _("Terminals"); // Fake name, as for every terminal
  // Where an agent runs says where its terminal is: a host to log in to, or the
  // shell (a WSL distribution) of the agent. A terminal has these itself.
  if (m_agent) {
    terminal.loginShell = m_agent->loginShell;
    terminal.remoteHost = m_agent->remoteHost;
    terminal.remoteUser = m_agent->remoteUser;
  } else {
    terminal.loginShell = m_session.loginShell;
    terminal.remoteHost = m_session.remoteHost;
    terminal.remoteUser = m_session.remoteUser;
  }
  if (m_session.IsTmuxSession()) {
    // Its own tmux session: the name of this one would attach to it.
    terminal.tmuxName = m_session.GetTerminalTmuxName();
  }
  return new SessionPage(parent, std::nullopt, std::move(terminal),
                         /*resume=*/false, /*owner=*/this);
}

void SessionPage::InitStatus() {
  // Where the agent runs: this machine, a WSL distro or a remote host.
  m_host = _("Local");
  if (m_agent && m_agent->IsRemote()) {
    m_host = m_agent->remoteUser.empty()
                 ? m_agent->remoteHost
                 : m_agent->remoteUser + "@" + m_agent->remoteHost;
  } else if (m_agent && m_agent->IsWSL()) {
    m_host = wxString::Format(_("WSL: %s"), wsl::DistroOf(m_agent->loginShell));
  } else if (!m_agent && !m_session.remoteHost.empty()) {
    // A terminal that logs in to a remote host.
    m_host = m_session.remoteUser.empty()
                 ? m_session.remoteHost
                 : m_session.remoteUser + "@" + m_session.remoteHost;
  }
  UpdateSessionLabel();
}

void SessionPage::UpdateSessionLabel() {
  const wxString agent = m_session.plainTerminal || m_session.agentName.empty()
                             ? wxString(_("Terminal"))
                             : m_session.agentName;
  m_sessionLabel = m_session.name + " - " + agent;
  // Reads the icon from disk: only when the session changes.
  m_sessionIcon = SessionIconFor(m_session, 16);
  PublishStatus();
}

void SessionPage::PublishStatus() {
  // Only the page that is showing owns the status bar of the window.
  if (!IsActive() || !IsShown()) {
    return;
  }
  SessionStatusEvent event(wxEVT_SESSION_STATUS);
  event.SetEventObject(this);
  event.SetString(m_statusText);
  event.SetReviewText(m_reviewText);
  // A session in tmux shows the name of its tmux session next to the host.
  event.SetHost(m_session.IsTmuxSession()
                    ? m_host + " | tmux: " + m_session.tmuxName
                    : m_host);
  event.SetSessionLabel(m_sessionLabel);
  event.SetIcon(m_sessionIcon);
  event.SetBusy(m_busy);
  // The fields cut long texts: all of them are in the tooltip.
  wxString tooltip;
  if (!m_reviewText.empty()) {
    tooltip << m_reviewText << wxT("\n");
  }
  tooltip << m_sessionLabel << wxT("\n") << m_host;
  if (!m_session.workingDir.empty()) {
    tooltip += ": " + m_session.workingDir;
  }
  if (m_session.IsTmuxSession()) {
    tooltip << wxT("\n") << _("tmux session:") << " " << m_session.tmuxName;
  }
  event.SetTooltip(tooltip);
  // No handler here: it goes up to MainFrame.
  GetEventHandler()->ProcessEvent(event);
}

void SessionPage::UpdateStatus() {
  const bool inProgress = m_review && m_review->IsInProgress();
  const bool ended = m_review && !inProgress;
  if (!ended) {
    m_endNoticeShown = false;
  } else if (!m_endNoticeShown) {
    // Tell the user how the review ended (once).
    m_endNoticeShown = true;
    m_notice = m_review->StatusText();
    m_noticeTimer.Start(kNoticeMs, wxTIMER_ONE_SHOT);
  }

  if (inProgress) {
    // The terminal title is not followed while a review is in progress; the
    // status bar keeps the one it had (the current one if it had none).
    if (m_statusText.empty()) {
      m_statusText = m_terminalTitle;
    }
    m_reviewText = m_review->StatusText();
  } else {
    m_statusText = m_terminalTitle;
    m_reviewText = m_noticeTimer.IsRunning() ? m_notice : wxString();
  }
  // Waiting for the user (stalled) is not busy.
  m_busy = inProgress && !m_review->HasStalled();
  PublishStatus();
}

void SessionPage::ShowNotice(const wxString &text) {
  m_notice = text;
  m_noticeTimer.Start(kNoticeMs, wxTIMER_ONE_SHOT);
  UpdateStatus();
}

void SessionPage::ClearNotice() {
  m_noticeTimer.Stop();
  m_notice.clear();
  m_endNoticeShown = false;
  UpdateStatus();
}

SessionPage::~SessionPage() {
  m_alive->store(false);
  EventNotifier::Get()->Unbind(wxEVT_TMUX_MACHINE, &SessionPage::OnTmuxMachine,
                               this);
  // The review folder is deleted with the ReviewBuddy: do it after the
  // reviewer's pane is asked to go, not as a member destroyed before it. No
  // notice, focus or unsplit here: the page is being torn down.
  DestroyReviewPane();
  m_review.reset();
  // The file of the first message, if the agent did not delete it (or never
  // started).
  DeletePromptFile();
}

void SessionPage::DeletePromptFile() {
  const PromptFile file = m_session.promptFile;
  m_session.promptFile = PromptFile{};
  // An agent in tmux goes on without the page (Archive, Kennel closing): when
  // its first message was typed, it may not have read the file yet. The file
  // is its own then (the message asks it to delete it).
  const bool typed = m_session.initialPrompt.empty();
  if (m_session.IsTmuxSession() && typed) {
    return;
  }
  PromptFileRemover::Get().Remove(file);
}

void SessionPage::DestroyReviewPane() {
  if (m_reviewTerminal != nullptr) {
    m_reviewTerminal->Destroy(); // and its progress bar; the pane stays
  }
  m_reviewTerminal = nullptr;
  // No update here: the page may be going away. CloseReviewBuddy() does it.
  m_reviewActivity = ProgramActivity::None;
  m_reviewerName.clear();
  m_reviewerWorking = true;
  m_reviewAcceleratorInterceptor.reset();
}

bool SessionPage::IsActive() const {
  return GetMainFrame()->IsWindowActive(this);
}

void SessionPage::SetDefaultSessionName(const wxString &name) {
  m_defaultTitle = MakeAppTitle(name, m_session.agentName);
  m_session.name = name;
  UpdateSessionLabel();
  ApplyTitle();
}

void SessionPage::CreateTerminal() {
  m_programStarted = false; // Of the terminal that this creates
  // Phase 1 of starting a session (the caller then calls StartProgram()):
  // creates the terminal and starts its shell. The page has a terminal after
  // it.
  std::map<wxString, wxString> env;
  if (m_agent) {
    env = m_agent->env;
  }

  std::optional<wxString> cwd;
  // The folder of a terminal on a remote host is a folder of the host: the
  // local shell that logs in there starts where it starts.
  if (!m_session.workingDir.empty() && m_session.remoteHost.empty()) {
    cwd = m_session.workingDir;
  }

  const auto &prefs = AppManager::Get().GetPrefs();
  // Use the default login shell
  wxString shellCommand = prefs.terminalLoginShell;

  if (m_session.plainTerminal) {
    const auto &shells = ::FindShells().shells;
    if (!m_session.loginShell.empty()) {
      // A restored terminal runs the shell it had, as long as it is still
      // there. The terminal of the bottom pane runs the shell of its agent as
      // it is (the machine of the agent is not always in the list).
      const bool found =
          m_owner != nullptr ||
          std::any_of(shells.begin(), shells.end(), [this](const auto &shell) {
            return shell.second == m_session.loginShell;
          });
      if (found) {
        shellCommand = m_session.loginShell;
      } else {
        KLOG_WARN() << "Shell of '" << m_session.name
                    << "' is gone: " << m_session.loginShell;
        m_session.loginShell.clear();
      }
    }

    // No shell (a terminal that was not given one, or whose shell is gone): the
    // default one. The user is asked before the terminal is made, see
    // MainNotebook::StartTerminal().
  }

  if (m_agent) {
    if (!m_agent->loginShell.empty()) {
      // This agent has a custom shell -> use it
      shellCommand = m_agent->loginShell;
    }
    if (!m_agent->remoteHost.empty()) {
      KLOG_DEBUG() << "Launching remote session: " << m_session.name;
      cwd.reset();
      env.clear();
    } else {
      if (!m_session.workingDir.empty()) {
        wxFileName::Mkdir(m_session.workingDir, wxS_DIR_DEFAULT,
                          wxPATH_MKDIR_FULL);
      }
      KLOG_DEBUG() << "Launching local session '" << m_session.name << "'"
                   << (cwd ? wxString(" (cwd=" + *cwd + ")") : wxString());
    }
  }

  m_terminal = TerminalManager::Get().CreateTerminal(
      m_agentPanel, {.shellCommand = shellCommand,
                     .workingDir = m_session.workingDir,
                     .cwd = cwd,
                     .env = env});
  AddToPane(m_agentPanel, m_terminal);
  m_acceleratorInterceptor =
      std::make_unique<AcceleratorInterceptor>(m_terminal);
  GetSizer()->Layout();
  m_terminal->RegisterTextClickedFilter([](const std::string &) -> bool {
    return true; /* consumed */
  });
  m_terminal->Bind(wxEVT_TERMINAL_TERMINATED, &SessionPage::OnTerminated, this);
  m_terminal->Bind(wxEVT_TERMINAL_APP_SELECTION_CHANGED,
                   &SessionPage::OnAppSelectionChanged, this);
  m_terminal->Bind(wxEVT_TERMINAL_TITLE_CHANGED, &SessionPage::OnTitleChanged,
                   this);
  m_terminal->Bind(wxEVT_TERMINAL_CWD_CHANGED, &SessionPage::OnCwdChanged,
                   this);
  m_terminal->Bind(wxEVT_TERMINAL_PROGRAM_STATUS, &SessionPage::OnProgramStatus,
                   this);

  ConfigureTerminal(m_terminal);
  // After the theme (the text colour follows the background) and before the
  // terminal starts, so the program gets the right number of rows.
  UpdateHeader();
  m_terminal->EnsureStarted();
  SetStatus(SessionStatus::Running);
  CheckGitRepo();
}

void SessionPage::OnAppSelectionChanged(wxTerminalEvent &event) {
  event.Skip();
  // Tabs in the background must not overwrite the active tab's text, nor the
  // clipboard: a program there could put a command on it without the user
  // seeing it.
  if (wxWindow::FindFocus() != m_terminal) {
    return;
  }

  // The program copied text with OSC 52: put it on the system clipboard. An
  // empty string means the text was cleared, which leaves the clipboard alone.
  const wxString &text = event.GetString();
  if (!text.empty() && wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Flush();
    wxTheClipboard->Close();
  }
}

void SessionPage::FocusTerminal(wxTerminalViewCtrl *terminal) {
  if (terminal == nullptr || !IsActive() || !IsShownOnScreen()) {
    return;
  }
  wxWindow *focused = wxWindow::FindFocus();
  if (focused != nullptr && focused != this && !IsDescendant(focused)) {
    return;
  }
  // Also when the application window is not active: the focus is restored to
  // this terminal when it is.
  terminal->SetFocus();
}

void SessionPage::AddToPane(wxPanel *pane, wxTerminalViewCtrl *terminal) {
  pane->GetSizer()->Add(terminal, wxSizerFlags(1).Expand());
  // A thin progress bar for what the program reports (OSC 9;4 and 7501). It
  // needs the sizer of the pane, so it comes after the terminal is added.
  terminal->ShowProgressBar(true);
}

void SessionPage::ConfigureTerminal(wxTerminalViewCtrl *terminal) {
  terminal->Bind(wxEVT_TERMINAL_TEXT_LINK, &SessionPage::OnTerminalLink, this);
  // Bound after the terminal's own handler, so it runs first; it does not call
  // Skip(), which keeps the terminal's Copy / Paste / Clear menu from showing.
  terminal->Bind(wxEVT_CONTEXT_MENU, &SessionPage::OnTerminalContextMenu, this);
}

std::vector<wxTerminalViewCtrl *> SessionPage::GetTerminals() const {
  std::vector<wxTerminalViewCtrl *> terminals;
  if (m_terminal != nullptr) {
    terminals.push_back(m_terminal);
  }
  if (m_reviewTerminal != nullptr) {
    terminals.push_back(m_reviewTerminal);
  }

  if (m_terminalPage && m_terminalPage->GetTerminal()) {
    terminals.push_back(m_terminalPage->GetTerminal());
  }
  return terminals;
}

std::vector<wxString> SessionPage::ProgramCommands() const {
  if (m_session.IsJobRun()) {
    return m_session.jobCommands;
  }

  // In tmux when the session was made to (the name is kept with it) and the
  // machine has said that it has tmux (see StartProgram()).
  std::optional<tmux::LaunchSpec> spec;
  if (m_session.IsTmuxSession() && m_tmuxVersion) {
    const auto &prefs = AppManager::Get().GetPrefs();
    spec = tmux::LaunchSpec{};
    spec->server = m_session.GetTmuxServer();
    spec->sessionName = m_session.tmuxName;
    spec->settings = tmux::AdaptToVersion(
        tmux::ParseCommands(spec->server == tmux::Server::kAgent
                                ? prefs.tmuxAgentSettings
                                : prefs.tmuxTerminalSettings),
        *m_tmuxVersion);
  }

  if (m_agent) {
    return BuildCommandLine(*m_agent, m_session.workingDir, m_resume,
                            m_session.initialPrompt, spec);
  }

  // A terminal is a shell of this machine. On a remote host it logs in with ssh
  // first (also when it is restored), and the folder is one of the host.
  std::vector<wxString> commands;
  const bool remote = !m_session.remoteHost.empty();
  if (remote) {
    commands.push_back(
        SshLoginCommand(m_session.remoteHost, m_session.remoteUser));
  }
  if (spec) {
    // Its shell, through the shim, in tmux. On a host the shell first goes to
    // the folder of the session, with `$HOME` and `~` expanded there (only when
    // the tmux session is made: an attach keeps its own folder, and a folder
    // that is not there leaves the shell in the home folder).
    wxString command = tmux::TerminalCommand(tmuxshim::ShellPath());
    if (remote && !m_session.workingDir.empty()) {
      command =
          ChangeDirCommand(m_session.workingDir) + " 2>/dev/null; " + command;
    }
    spec->command = command;
    commands.push_back(tmux::AttachOrCreate(*spec));
  } else if (remote && !m_session.workingDir.empty()) {
    commands.push_back(ChangeDirCommand(m_session.workingDir));
  }
  return commands; // Nothing for a terminal of this machine: it is its shell
}

Account SessionPage::GetTmuxMachine() const {
  return m_session.GetTmuxAccount(
      m_agent, AppManager::Get().GetPrefs().terminalLoginShell);
}

void SessionPage::StartProgram() {
  CHECK_NOT_NULL_RETURN(m_terminal);
  if (m_session.IsTmuxSession()) {
    const Account machine = GetTmuxMachine();
    if (TmuxClient::IsAvailableFor(machine)) {
      // The machine is asked on a worker thread, once for all the sessions that
      // need it (TmuxMachines): OnTmuxMachine() calls FinaliseTerminalCreate()
      // when it is ready. Until then, the terminal is there and nothing is
      // typed into it.
      m_waitingForTmux = true;
      SetNote(
          wxString::Format(_("Connecting to %s..."), machine.GetDisplayName()));
      TmuxMachineEvent request(wxEVT_TMUX_MACHINE_PREPARE);
      request.SetAccount(machine);
      request.SetForced(m_askMachineAgain);
      m_askMachineAgain = false;
      EventNotifier::Get()->ProcessEvent(request);
      return;
    }
    DropTmux(); // This machine runs its sessions without tmux
  }
  FinaliseTerminalCreate();
}

void SessionPage::OnTmuxMachine(TmuxMachineEvent &event) {
  // Every page gets it: the next listener too.
  event.Skip();
  if (!m_waitingForTmux ||
      event.GetAccount().GetId() != GetTmuxMachine().GetId()) {
    return;
  }
  const wxString where = event.GetAccount().GetDisplayName();
  switch (event.GetState()) {
  case TmuxMachineEvent::State::kProbing:
    HideInfo();
    SetNote(wxString::Format(_("Connecting to %s..."), where));
    return;
  case TmuxMachineEvent::State::kNeedsShim: {
    // A question: this page shows it, with its own button (every session of the
    // machine does, and one answer is enough).
    SetNote({});
    ShowInfo(wxString::Format(
                 _("%s %s is not installed on %s. It lets Kennel show what the "
                   "agent is doing (working, done, waiting). Download it, or "
                   "close this session."),
                 tmuxshim::Tool().name, tmuxshim::Tool().version, where),
             wxICON_QUESTION, /*download=*/true, /*close=*/true);
    return;
  }
  case TmuxMachineEvent::State::kInstalling:
    SetNote({});
    ShowInfo(wxString::Format(_("Downloading %s to %s..."),
                              tmuxshim::Tool().name, where),
             wxICON_INFORMATION, /*download=*/false, /*close=*/true);
    return;
  case TmuxMachineEvent::State::kReady:
    m_waitingForTmux = false;
    m_tmuxVersion = event.GetVersion();
    HideInfo();
    SetNote({});
    FinaliseTerminalCreate();
    return;
  case TmuxMachineEvent::State::kUnavailable:
    // Nothing is typed: a session that is in tmux already must not get a second
    // one because the machine could not be asked. The user may try again
    // (Refresh), or close the session.
    m_waitingForTmux = false;
    KLOG_WARN() << "tmux for '" << m_session.name << "': " << event.GetString();
    SetNote({});
    ShowInfo(event.GetString() + " " +
                 _("Use Refresh to try again, or close this session."),
             wxICON_WARNING, /*download=*/false, /*close=*/true);
    return;
  }
}

void SessionPage::ShowInfo(const wxString &text, int icon, bool download,
                           bool close) {
  // The bar is asked which buttons it has: it asserts when a button that it
  // does not have is removed.
  const auto setButton = [this](int id, const wxString &label, bool wanted) {
    const bool has = m_infoBar->HasButtonId(id);
    if (wanted && !has) {
      m_infoBar->AddButton(id, label);
    } else if (!wanted && has) {
      m_infoBar->RemoveButton(id);
    }
  };
  setButton(ID_TMUX_DOWNLOAD, _("Download"), download);
  setButton(ID_TMUX_CLOSE, _("Close session"), close);
  // The Cancel of a handoff is shown with its own message only.
  setButton(ID_HANDOFF_CANCEL, _("Cancel"), false);
  m_infoBar->ShowMessage(text, icon);
}

bool SessionPage::IsAgentRunning() const {
  // Its program was typed (not while the page waits for tmux), and it did not
  // end (Running, not Exited or Detached).
  return m_agent && !IsPlainTerminal() && !m_session.IsJobRun() &&
         m_terminal != nullptr && m_programStarted && !m_waitingForTmux &&
         m_status == SessionStatus::Running;
}

bool SessionPage::CanStartHandoff() const {
  // What the agent reports (OSC 7501): not while it works, and not while it
  // waits for an answer (the prompt would be the answer). An agent that
  // reports nothing is None.
  const bool agentFree = m_terminalActivity == ProgramActivity::None ||
                         m_terminalActivity == ProgramActivity::Done;
  return !m_handoff && IsAgentRunning() && agentFree;
}

bool SessionPage::SendPrompt(const wxString &text) {
  // Not while the agent waits for an answer (OSC 7501): the message would be
  // the answer.
  if (!IsAgentRunning() || m_terminalActivity == ProgramActivity::Blocked) {
    return false;
  }
  m_terminal->SendInput(text.ToStdString(wxConvUTF8));
  // In most agents the text after a paste is a new line until the paste is
  // done: Enter comes a little later (as Review Buddy does).
  m_promptEnterTimer.StartOnce(kPromptEnterDelayMs);
  return true;
}

void SessionPage::OnPromptEnterTimer(wxTimerEvent &) {
  if (m_terminal != nullptr) {
    m_terminal->SendEnter();
  }
}

void SessionPage::StartHandoff() {
  if (!CanStartHandoff()) {
    return;
  }
  m_handoff = std::make_unique<SessionHandoff>(
      HandoffPlace(*m_agent, m_session.workingDir), m_terminal);
  m_handoff->Bind(wxEVT_SESSION_SUMMARY_READY, &SessionPage::OnHandoffReady,
                  this);
  m_handoff->Bind(wxEVT_SESSION_SUMMARY_FAILED, &SessionPage::OnHandoffFailed,
                  this);
  // The message, with its Cancel button only.
  for (const int id :
       {static_cast<int>(ID_TMUX_DOWNLOAD), static_cast<int>(ID_TMUX_CLOSE)}) {
    if (m_infoBar->HasButtonId(id)) {
      m_infoBar->RemoveButton(id);
    }
  }
  if (!m_infoBar->HasButtonId(ID_HANDOFF_CANCEL)) {
    m_infoBar->AddButton(ID_HANDOFF_CANCEL, _("Cancel"));
  }
  m_infoBar->ShowMessage(
      _("The agent writes a summary of this session for a new one..."),
      wxICON_INFORMATION);
  m_handoff->Begin();
}

void SessionPage::OnHandoffReady(wxCommandEvent &event) {
  HideInfo();
  // The notebook asks for the new session (a new event: this one is the
  // handoff's).
  wxCommandEvent ready{wxEVT_SESSION_SUMMARY_READY};
  ready.SetEventObject(this);
  ready.SetString(event.GetString());
  Notify(ready);
  EndHandoff();
}

void SessionPage::OnHandoffFailed(wxCommandEvent &event) {
  ShowInfo(
      wxString::Format(_("No summary of this session: %s"), event.GetString()),
      wxICON_WARNING, false, false);
  EndHandoff();
}

void SessionPage::OnHandoffCancel(wxCommandEvent &) {
  HideInfo();
  // Ctrl+C to the agent; not from an event of the handoff: it can go now.
  if (m_handoff) {
    m_handoff->Cancel();
    m_handoff.reset();
  }
}

void SessionPage::EndHandoff() {
  // Deferred: the handoff is the one that sends the event that is handled.
  CallAfter([this] { m_handoff.reset(); });
}

void SessionPage::HideInfo() {
  if (m_infoBar->IsShown()) {
    m_infoBar->Dismiss();
  }
}

void SessionPage::OnTmuxDownload(wxCommandEvent &) {
  // The machine downloads it once; it says "installing" to every page that
  // waits for it, and "ready" when it is done. The bar changes then, not in
  // the handler of its own button.
  TmuxMachineEvent request(wxEVT_TMUX_MACHINE_INSTALL_SHIM);
  request.SetAccount(GetTmuxMachine());
  EventNotifier::Get()->ProcessEvent(request);
}

void SessionPage::OnTmuxClose(wxCommandEvent &) {
  // The user closes the session: the notebook ends its tmux session on the
  // machine and closes the tab (an exit does not stop the tmux session).
  m_waitingForTmux = false;
  wxCommandEvent event{wxEVT_SESSION_CLOSE_REQUESTED};
  event.SetString(m_session.name);
  Notify(event);
}

void SessionPage::SendCommands(const std::vector<wxString> &commands) {
  if (m_programStarted && m_terminal != nullptr) {
    for (const wxString &command : commands) {
      m_terminal->SendCommand(command);
    }
    return;
  }
  m_commandsAfterStart.insert(m_commandsAfterStart.end(), commands.begin(),
                              commands.end());
}

void SessionPage::FinaliseTerminalCreate() {
  CHECK_NOT_NULL_RETURN(m_terminal);
  for (const wxString &command : ProgramCommands()) {
    KLOG_DEBUG() << "Session '" << m_session.name << "' > " << command;
    m_terminal->SendCommand(command);
  }
  // The first message is for the first start only (see Session::initialPrompt).
  // Its file stays the page's until the page goes: the agent may not start, or
  // not delete it.
  m_session.initialPrompt.clear();
  m_programStarted = true;
  const std::vector<wxString> after = std::move(m_commandsAfterStart);
  m_commandsAfterStart.clear();
  for (const wxString &command : after) {
    m_terminal->SendCommand(command);
  }
}

void SessionPage::DropTmux() {
  m_session.tmuxName.clear();
  // The workspace is saved without it.
  wxCommandEvent event{wxEVT_SESSION_CHANGED};
  event.SetEventObject(this);
  Notify(event);
}

void SessionPage::SetNote(const wxString &text) {
  // The strip at the bottom of the terminal is the header of the session (see
  // UpdateHeader()): while there is a note, it shows the note, and the header
  // comes back when the note ends.
  m_note = text;
  UpdateHeader(true);
}

void SessionPage::SetFocus() {
  if (m_terminal)
    m_terminal->SetFocus();
}

void SessionPage::Restart() {
  KLOG_DEBUG() << "Restarting session '" << m_session.name << "'";
  CloseReviewBuddy();
  // A handoff waits for the agent of the terminal that goes: it ends.
  if (m_handoff) {
    m_handoff.reset();
    HideInfo();
  }
  // Only the agent's terminal restarts: the terminal of the bottom pane goes on
  // (it is not the agent).
  if (m_terminal != nullptr) {
    m_terminal->Destroy(); // and its progress bar; the pane stays
    m_terminal = nullptr;
    m_acceleratorInterceptor.reset();
  }
  m_terminalActivity = ProgramActivity::None;
  UpdateProgramActivity();
  const bool wasDetached = m_status == SessionStatus::Detached;
  m_status = SessionStatus::Starting;
  if (wasDetached) {
    RefreshTabIcon(); // The badge is not yellow any more
  }
  ++m_endedCheck; // The answer of a question about the old terminal is stale
  m_resume = true;
  // The machine is asked again: tmux may have been installed, or the host may
  // be there now. An answer that comes for the old terminal is not waited for.
  m_tmuxVersion.reset();
  m_waitingForTmux = false;
  m_askMachineAgain = true;
  HideInfo();
  m_note.clear();
  // The commands that wait stay: the new terminal types them once its program
  // is started (the user asked for them).
  m_programStarted = false;
  CallAfter([this] {
    CreateTerminal();
    StartProgram();
  });
}

void SessionPage::OnTerminated(wxTerminalEvent &evt) {
  evt.Skip();
  // The program is gone at once, also while a tmux session is asked about:
  // nothing may be typed for it any more (CanStartHandoff()).
  m_programStarted = false;
  m_terminalActivity = ProgramActivity::None;
  UpdateProgramActivity();
  // Only a session that really runs in tmux can outlive its terminal.
  if (m_session.IsTmuxSession()) {
    if (m_tmuxVersion) {
      CheckTmuxSessionAfterTerminated();
      return;
    }
    // The shell ended before the machine was ready (or before it said what is
    // wrong with it): the tmux session, if there is one, is not ours to end,
    // and the tab must not go. Refresh tries again.
    m_waitingForTmux = false;
    SetStatus(SessionStatus::Detached);
    SetNote(_("The terminal ended. Use Refresh to start the session again."));
    return;
  }
  SetStatus(SessionStatus::Exited);
}

void SessionPage::CheckTmuxSessionAfterTerminated() {
  const Account account = GetTmuxMachine();
  const tmux::Server server = m_session.GetTmuxServer();
  const int generation = ++m_endedCheck;
  // Blocks (ssh, wsl.exe): a thread of its own, nothing of the page is used
  // until the answer is back on the UI thread.
  std::thread([this, alive = m_alive, account, server, generation,
               name = m_session.tmuxName] {
    const StatusOr<bool> running =
        TmuxClient::HasSession(account, server, name);
    const bool answered = running.ok();
    const bool isRunning = answered && running.value();
    const wxString why = answered ? wxString() : running.status().message();
    CallAfterIfAlive(alive, [this, generation, answered, isRunning, why] {
      OnTmuxSessionChecked(generation, answered, isRunning, why);
    });
  }).detach();
}

void SessionPage::OnTmuxSessionChecked(int generation, bool answered,
                                       bool running, const wxString &why) {
  if (generation != m_endedCheck) {
    return; // Restart() ran since: this terminal is gone
  }
  if (answered && !running) {
    SetStatus(SessionStatus::Exited); // The program ended: the tab closes
    return;
  }
  if (!answered) {
    KLOG_WARN() << "Could not ask if the tmux session " << m_session.tmuxName
                << " is running: " << why;
  }
  SetStatus(SessionStatus::Detached);
  // The tab stays in both cases (a host that is out of reach must not lose the
  // session), but the note says what is known.
  SetNote(answered
              ? _("Detached from tmux. Use Refresh to attach again.")
              : wxString::Format(_("Could not reach tmux: %s. Use Refresh to "
                                   "try again."),
                                 why));
}

void SessionPage::OnProgramStatus(wxTerminalEvent &evt) {
  evt.Skip();
  // Only the root record (no id) says what the program as a whole is doing.
  if (!evt.GetProgramId().empty()) {
    return;
  }
  ProgramActivity activity = ProgramActivity::None;
  switch (evt.GetProgramState()) {
  case wxTerminalProgramState::Working:
    activity = ProgramActivity::Working;
    break;
  case wxTerminalProgramState::Done:
    activity = ProgramActivity::Done;
    break;
  case wxTerminalProgramState::Blocked:
    activity = ProgramActivity::Blocked;
    break;
  case wxTerminalProgramState::Idle:
  default:
    break;
  }
  // The event is pending: the review buddy's terminal may be gone. Compare the
  // pointer only.
  if (m_reviewTerminal != nullptr && evt.GetEventObject() == m_reviewTerminal) {
    m_reviewActivity = activity;
  } else {
    m_terminalActivity = activity;
  }
  UpdateProgramActivity();
}

void SessionPage::UpdateProgramActivity() {
  // What needs the user first: Blocked, then Working, then Done.
  constexpr ProgramActivity kPriority[] = {ProgramActivity::Blocked,
                                           ProgramActivity::Working,
                                           ProgramActivity::Done};
  ProgramActivity result = ProgramActivity::None;
  for (ProgramActivity candidate : kPriority) {
    if (m_terminalActivity == candidate || m_reviewActivity == candidate) {
      result = candidate;
      break;
    }
  }
  SetProgramActivity(result);
}

void SessionPage::AcknowledgeDone() {
  for (ProgramActivity *activity : {&m_terminalActivity, &m_reviewActivity}) {
    if (*activity == ProgramActivity::Done) {
      *activity = ProgramActivity::None;
    }
  }
  UpdateProgramActivity();
}

void SessionPage::SetProgramActivity(ProgramActivity activity) {
  if (m_programActivity == activity) {
    return;
  }
  m_programActivity = activity;
  RefreshTabIcon();
}

void SessionPage::RefreshTabIcon() {
  wxCommandEvent e{wxEVT_SESSION_PROGRAM};
  e.SetEventObject(this);
  Notify(e);
}

TabBadge SessionPage::GetTabBadge() const {
  if (m_status == SessionStatus::Detached) {
    return TabBadge::kDetached;
  }
  return m_session.IsTmuxSession() ? TabBadge::kTmux : TabBadge::kNone;
}

void SessionPage::UpdateHeaderColours(wxTerminalViewCtrl *ctrl) {
  if (ctrl == nullptr)
    return;
  ReviewBuddyHeader::SetText(ctrl, ctrl->GetFooterText(), true);
}

void SessionPage::UpdateHeader(bool force) {
  if (m_terminal == nullptr) {
    return;
  }
  // While a review buddy is open, its header shows the state of the review.
  if (m_reviewHeader) {
    if (force) {
      m_reviewHeader->Refresh();
    }
    return;
  }
  // "Agent name | Title | Working directory"; a plain terminal is a
  // "Terminal".
  const wxString head = m_session.plainTerminal || m_session.agentName.empty()
                            ? wxString(_("Terminal"))
                            : m_session.agentName;
  // A note (tmux is connecting, why it could not be used) takes the place of
  // the header until it is cleared: the terminal has one strip, and the title
  // and the folder change it at each prompt.
  ReviewBuddyHeader::SetText(
      m_terminal,
      m_note.empty() ? JoinHeader(head, m_terminalTitle, GetCurrentDir())
                     : m_note,
      force);
}

void SessionPage::UpdateReviewHeader(bool force) {
  if (m_reviewTerminal == nullptr) {
    return;
  }
  const wxString head = m_reviewerName.empty()
                            ? wxString(_("Review Buddy"))
                            : m_reviewerName + " (" + _("Review Buddy") + ")";
  // Not JoinHeader(): this header has a state, no title or folder.
  const wxString state =
      m_reviewerWorking ? _("Reviewing changes") : _("Waiting");
  ReviewBuddyHeader::SetText(m_reviewTerminal, head + " | " + state, force);
}

wxString SessionPage::GetCurrentDir() const {
  if (m_terminal != nullptr && !m_terminal->GetCurrentDir().empty()) {
    return m_terminal->GetCurrentDir();
  }
  return m_session.workingDir;
}

void SessionPage::OnCwdChanged(wxTerminalEvent &evt) {
  evt.Skip();
  UpdateHeader();
  // The list of the terminals shows the folder.
  wxCommandEvent e{wxEVT_SESSION_CWD};
  e.SetEventObject(this);
  Notify(e);
}

void SessionPage::OnTitleChanged(wxTerminalEvent &evt) {
  evt.Skip();
  // The status bar shows it (not while a review is in progress).
  m_terminalTitle = evt.GetTitle();
  UpdateStatus();
  UpdateHeader();
}

void SessionPage::SetStatus(SessionStatus status) {
  if (m_status == status) {
    return;
  }
  m_status = status;
  switch (m_status) {
  case SessionStatus::Exited: {
    // The program ended (also its tmux session): nobody reads the file of its
    // first message any more.
    PromptFileRemover::Get().Remove(m_session.promptFile);
    m_session.promptFile = PromptFile{};
    wxCommandEvent e{wxEVT_SESSION_EXITED};
    e.SetString(m_session.name);
    Notify(e);
    break;
  }
  case SessionStatus::Detached:
    RefreshTabIcon(); // The badge is yellow now
    break;
  default:
    break;
  }
}

void SessionPage::OnTerminalLink(wxTerminalEvent &evt) {
  evt.Skip();
  wxString text = evt.GetClickedText();
  while (text.EndsWith(".") || text.EndsWith(":") || text.EndsWith(","))
    text.RemoveLast();

  // Nothing is left of a click on "." or "..", for example.
  if (text.empty()) {
    return;
  }

  wxString textLower = text.Lower();

  if (textLower.StartsWith("http://") || textLower.StartsWith("https://")) {
    ::wxLaunchDefaultBrowser(text);
    return;
  }
  if (textLower.StartsWith("file://")) {
    ::wxLaunchDefaultApplication(text);
    return;
  }

  // Where a relative path may start: the folder the shell reported last (it
  // follows "cd"), then the folder the session started in.
  std::vector<wxString> baseDirs;
  if (auto *terminal = dynamic_cast<wxTerminalViewCtrl *>(evt.GetEventObject());
      terminal != nullptr && !terminal->GetCurrentDir().empty()) {
    baseDirs.push_back(terminal->GetCurrentDir());
  }
  if (!m_session.workingDir.empty() &&
      (baseDirs.empty() || baseDirs.back() != m_session.workingDir)) {
    baseDirs.push_back(m_session.workingDir);
  }

  if (const Account account = GetAccount(); account.IsRemote()) {
    // The path lives on the remote host (the agent runs there, or the terminal
    // logged in there): fetch it over SFTP.
    std::vector<wxString> searchDirs = baseDirs;
    if (m_agent) {
      searchDirs.push_back("$HOME/.kennel/sessions");
    }
    GetMainFrame()->GetMainView()->GetSessionsBook()->FindOrOpenFile(
        Account::Remote(account.m_name, account.m_user),
        ParseFileLocation(text), searchDirs);
    return;
  }

  // "path:line:column" (GCC, clang, ...): open the file at that place.
  FileLocation location = ParseFileLocation(text);
  bool foreignShell = false;
  wxString fullPath =
      ResolveLocalFile(m_agent, location.path, baseDirs, &foreignShell);
  if (fullPath.empty() && location.line > 0) {
    // Maybe the colon and the digits are part of the name.
    location = FileLocation{text};
    fullPath = ResolveLocalFile(m_agent, text, baseDirs, &foreignShell);
  }
  if (fullPath.empty()) {
    ShowNotice(
        foreignShell
            ? wxString::Format(
                  _("File not found, or it is on another machine: %s"), text)
            : wxString::Format(_("File not found: %s"), text));
    return;
  }
  if (::wxDirExists(fullPath) || !LooksLikeTextFile(fullPath)) {
    // Folders, images, PDFs, archives, ...: the OS knows best.
    ::wxLaunchDefaultApplication(fullPath);
    return;
  }
  // Deferred: don't rebuild the list from inside the terminal's mouse handler.
  location.path = fullPath;
  GetMainFrame()->GetMainView()->GetSessionsBook()->CallAfter(
      &MainNotebook::OpenFileAt, location);
}

void SessionPage::ApplyTheme(const wxTerminalTheme &theme) {
  for (auto *terminal : GetTerminals()) {
    terminal->SetTheme(theme);
  }
  auto terminals = GetTerminals();
  for (auto *terminal : terminals)
    UpdateHeaderColours(terminal);

  // The border around each terminal has the theme's background.
  for (wxWindow *pane : {
           static_cast<wxWindow *>(m_agentPanel),
           static_cast<wxWindow *>(m_reviewBuddyPanel),
           static_cast<wxWindow *>(m_temrinalPanel),
           static_cast<wxWindow *>(m_splitterMain),
           static_cast<wxWindow *>(m_splitterPageSession),
           static_cast<wxWindow *>(m_splitterReviewBuddy),
       }) {
    pane->SetBackgroundColour(theme.bg);
    pane->Refresh();
  }
}

void SessionPage::ApplyTitle() {
  if (IsActive() && IsShown()) {
    auto *frame = dynamic_cast<wxFrame *>(wxTheApp->GetTopWindow());
    frame->SetLabel(m_defaultTitle);
  }
}

// ---------------------------------------------------------------------------
// Context menu and review buddy
// ---------------------------------------------------------------------------

void SessionPage::OnTerminalContextMenu(wxContextMenuEvent &evt) {
  // There is no context menu: all the actions are in the toolbar. The event is
  // not skipped, which also hides the default menu of the terminal (this
  // handler is bound after the terminal's own, so it runs first).
  wxUnusedVar(evt);
}

void SessionPage::ApplyToolBarStyle() { toolbar::ApplyStyle(m_toolbar); }

void SessionPage::CreateToolBar() {
  auto &bmps = AppManager::Get().GetBitmaps();
  m_toolbar = new wxAuiToolBar(this);
  toolbar::ApplyStyle(m_toolbar);
  toolbar::AddTool(m_toolbar, ID_RESTART, _("Restart"),
                   bmps.Get("restart", false), _("Restart the session"));
  // Only an agent gets these tools: a terminal has no review buddy, and it is
  // that terminal already.
  if (!IsPlainTerminal()) {
    // The button asks which agent reviews, or opens the menu of a review that
    // is open.
    toolbar::AddTool(m_toolbar, ID_REVIEW_BUDDY, _("Review Buddy"),
                     bmps.Get("code-reviewer", false), _("Run Review Buddy"));
    toolbar::AddTool(m_toolbar, ID_TERMINAL, _("Terminal"),
                     bmps.Get("terminal", false),
                     _("Open a terminal in the folder and on the host of this "
                       "session (Ctrl+`)"));
  }
  m_toolbar->AddSeparator();
  toolbar::AddTool(m_toolbar, ID_COPY, _("Copy"), bmps.Get("copy", false),
                   _("Copy the selected text"));
  toolbar::AddTool(m_toolbar, ID_PASTE, _("Paste"), bmps.Get("paste", false),
                   _("Paste the clipboard text"));
  m_toolbar->AddSeparator();
  toolbar::AddTool(m_toolbar, ID_FIND, _("Find"), bmps.Get("find", false),
                   _("Find text in the terminal (Ctrl+F)"));
  m_toolbar->Realize();

  m_toolbar->Bind(wxEVT_TOOL, &SessionPage::OnToolRestart, this, ID_RESTART);
  m_toolbar->Bind(wxEVT_TOOL, &SessionPage::OnToolReviewBuddy, this,
                  ID_REVIEW_BUDDY);
  m_toolbar->Bind(wxEVT_TOOL, &SessionPage::OnToolTerminal, this, ID_TERMINAL);
  m_toolbar->Bind(wxEVT_UPDATE_UI, &SessionPage::OnToolHasTerminalUI, this,
                  ID_TERMINAL);
  m_toolbar->Bind(wxEVT_TOOL, &SessionPage::OnToolCopy, this, ID_COPY);
  m_toolbar->Bind(wxEVT_TOOL, &SessionPage::OnToolPaste, this, ID_PASTE);
  m_toolbar->Bind(wxEVT_TOOL, &SessionPage::OnToolFind, this, ID_FIND);
  m_toolbar->Bind(wxEVT_UPDATE_UI, &SessionPage::OnToolCopyUI, this, ID_COPY);
  m_toolbar->Bind(wxEVT_UPDATE_UI, &SessionPage::OnToolHasTerminalUI, this,
                  ID_PASTE);
  m_toolbar->Bind(wxEVT_UPDATE_UI, &SessionPage::OnToolHasTerminalUI, this,
                  ID_RESTART);
  m_toolbar->Bind(wxEVT_UPDATE_UI, &SessionPage::OnToolHasTerminalUI, this,
                  ID_FIND);
  m_toolbar->Bind(wxEVT_UPDATE_UI, &SessionPage::OnToolReviewBuddyUI, this,
                  ID_REVIEW_BUDDY);
}

wxTerminalViewCtrl *SessionPage::ActionTerminal() const {
  // The terminal that has the focus, or had it last (the agent's by default).
  for (auto *terminal : GetTerminals()) {
    if (terminal->HasFocus()) {
      return terminal;
    }
  }
  return SearchTerminal();
}

void SessionPage::OnToolRestart(wxCommandEvent &) {
  // Deferred: leave the toolbar's callback first, the restart destroys the
  // terminal.
  CallAfter(&SessionPage::Restart);
}

void SessionPage::OnToolTerminal(wxCommandEvent &) { ToggleTerminalPane(); }

bool SessionPage::CanToggleTerminalPane() const {
  // As the toolbar: an agent that has its terminal (a terminal is one already).
  return !IsPlainTerminal() && m_terminal != nullptr;
}

void SessionPage::ToggleTerminalPane() {
  if (!CanToggleTerminalPane()) {
    return;
  }
  // Shown: hide it (the terminal goes on running). Otherwise show it, and make
  // the terminal first if there is none.
  SetTerminalPane(m_terminalPage == nullptr || !m_splitterMain->IsSplit(),
                  /*focus=*/true);
}

void SessionPage::SetTerminalPane(bool shown, bool focus) {
  // The pane is shown first, and the terminal made after: it gets the size of
  // the pane, not the one of an empty panel.
  if (shown && !m_splitterMain->IsSplit()) {
    // Half each (see StartReviewer(): the default position is a stale one).
    m_splitterMain->SplitHorizontally(
        m_splitterPageSession, m_temrinalPanel,
        (m_splitterMain->GetClientSize().y - m_splitterMain->GetSashSize()) /
            2);
    m_temrinalPanel->Layout();
  } else if (!shown && m_splitterMain->IsSplit()) {
    m_splitterMain->Unsplit(m_temrinalPanel);
  }
  if (m_terminalPage == nullptr) {
    if (shown) {
      // Deferred: the pane has its size once the window has laid it out.
      CallAfter([this, focus] { CreateTerminalPage(focus); });
    } else {
      CreateTerminalPage(/*focus=*/false);
    }
    return;
  }
  if (focus) {
    (shown ? static_cast<wxWindow *>(m_terminalPage)
           : static_cast<wxWindow *>(m_terminal))
        ->SetFocus();
  }
  SaveTerminalPane();
}

void SessionPage::CreateTerminalPage(bool focus) {
  if (m_terminalPage != nullptr) {
    return; // Asked twice before the first one ran
  }
  m_terminalPage = Clone(m_temrinalPanel);
  if (m_terminalPage->GetTerminal() == nullptr) {
    // The terminal could not be made: no empty page in the pane.
    m_terminalPage->Destroy();
    m_terminalPage = nullptr;
    if (m_splitterMain->IsSplit()) {
      m_splitterMain->Unsplit(m_temrinalPanel);
    }
    SaveTerminalPane();
    return;
  }
  m_temrinalPanel->GetSizer()->Add(m_terminalPage, wxSizerFlags(1).Expand());
  m_temrinalPanel->Layout();
  if (focus && m_splitterMain->IsSplit()) {
    m_terminalPage->SetFocus();
  }
  SaveTerminalPane();
}

void SessionPage::Notify(wxCommandEvent &event) {
  // The notebook is the parent of a page, and goes by the name of the session.
  // A page in a bottom pane (Clone()) has the name of its owner, and is no page
  // of the notebook: what it says is not for the notebook. Only its end
  // matters, to its owner.
  if (m_owner != nullptr) {
    const wxEventType type = event.GetEventType();
    if (type == wxEVT_SESSION_EXITED || type == wxEVT_SESSION_CLOSE_REQUESTED) {
      // Deferred: this page is destroyed by it.
      m_owner->CallAfter(&SessionPage::CloseTerminalPage);
    }
    return;
  }
  GetParent()->GetEventHandler()->AddPendingEvent(event);
}

void SessionPage::SaveTerminalPane() {
  const bool has = m_terminalPage != nullptr;
  const bool shown = has && m_splitterMain->IsSplit();
  if (m_session.terminalPane == has && m_session.terminalPaneShown == shown) {
    return;
  }
  m_session.terminalPane = has;
  m_session.terminalPaneShown = shown;
  // The workspace is saved with it.
  wxCommandEvent event{wxEVT_SESSION_CHANGED};
  event.SetEventObject(this);
  Notify(event);
}

void SessionPage::CloseTerminalPage() {
  if (m_terminalPage == nullptr) {
    return;
  }
  m_splitterMain->Unsplit(m_temrinalPanel);
  m_terminalPage->Destroy();
  m_terminalPage = nullptr;
  if (m_terminal != nullptr) {
    m_terminal->SetFocus();
  }
  SaveTerminalPane();
}

void SessionPage::OnToolCopy(wxCommandEvent &) {
  wxTerminalViewCtrl *terminal = CopyTerminal();
  if (terminal == nullptr) {
    return;
  }
  if (terminal->CanCopy()) {
    // The text the user selected comes first.
    terminal->Copy();
  } else if (auto selection = terminal->GetAppSelection()) {
    // Otherwise the text the application copied (tmux, TUI apps like claude):
    // all of it, not only the first line.
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(new wxTextDataObject(*selection));
      wxTheClipboard->Close();
    }
  }
  // The toolbar click may have moved the focus.
  terminal->CallAfter(&wxWindow::SetFocus);
}

void SessionPage::OnToolPaste(wxCommandEvent &) {
  if (wxTerminalViewCtrl *terminal = ActionTerminal()) {
    terminal->Paste();
    terminal->CallAfter(&wxWindow::SetFocus);
  }
}

void SessionPage::OnToolFind(wxCommandEvent &) { ShowFind(); }

wxTerminalViewCtrl *SessionPage::CopyTerminal() const {
  // The one with the focus first, then the agent's, then the reviewer's.
  std::vector<wxTerminalViewCtrl *> terminals{ActionTerminal()};
  for (auto *terminal : GetTerminals()) {
    terminals.push_back(terminal);
  }
  for (auto *terminal : terminals) {
    if (terminal == nullptr) {
      continue;
    }
    const auto appSelection = terminal->GetAppSelection();
    if (terminal->CanCopy() || (appSelection && !appSelection->empty())) {
      return terminal;
    }
  }
  return nullptr;
}

void SessionPage::OnToolCopyUI(wxUpdateUIEvent &event) {
  event.Enable(CopyTerminal() != nullptr);
}

void SessionPage::OnToolHasTerminalUI(wxUpdateUIEvent &event) {
  event.Enable(m_terminal != nullptr);
}

void SessionPage::OnToolReviewBuddyUI(wxUpdateUIEvent &event) {
  // Only an agent can have a buddy (not a plain terminal or a scheduled job).
  event.Enable(SupportsReviewBuddy());
}

void SessionPage::OnToolReviewBuddy(wxCommandEvent &) {
  // The button asks which agent reviews, or opens the menu of the review that
  // is open.
  if (!m_review) {
    // Deferred: leave the toolbar's callback first.
    CallAfter(&SessionPage::ChooseReviewBuddy);
    return;
  }
  // There is a review: the menu to manage it.
  wxMenu menu;
  AppendReviewControls(menu);
  // Below the button: the top-left corner of the menu at the bottom-left
  // corner of the button.
  m_toolbar->SetToolSticky(ID_REVIEW_BUDDY, true);
  const wxRect rect = m_toolbar->GetToolRect(ID_REVIEW_BUDDY);
  m_toolbar->PopupMenu(&menu, rect.GetLeft(), rect.GetBottom() + 1);
  m_toolbar->SetToolSticky(ID_REVIEW_BUDDY, false);
  m_toolbar->Refresh();
}

void SessionPage::AppendReviewControls(wxMenu &menu) {
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
}

std::vector<AgentDef> SessionPage::GetReviewBuddyChoices() {
  if (m_review || !m_agent || m_session.IsJobRun()) {
    return {};
  }
  RefreshGitState();
  wxString whyNot;
  if (!CanHaveReviewBuddy(whyNot)) {
    return {};
  }
  return GetReviewerCandidates();
}

void SessionPage::StartReviewBuddy(const wxString &agentName) {
  for (const AgentDef &agent : GetReviewBuddyChoices()) {
    if (agent.name == agentName) {
      // Deferred: the caller may be a handler of a dialog.
      CallAfter(&SessionPage::LaunchReviewBuddy, agent);
      return;
    }
  }
}

void SessionPage::StopReviewBuddy() {
  if (m_review) {
    CallAfter(&SessionPage::CloseReviewBuddy);
  }
}

bool SessionPage::SupportsReviewBuddy() const {
  return m_terminal != nullptr && m_agent && !m_session.IsJobRun();
}

void SessionPage::ChooseReviewBuddy() {
  if (m_review) {
    return;
  }
  std::vector<AgentDef> reviewers;
  const wxString why = GetReviewBuddyUnavailableReason(&reviewers);
  if (!why.empty()) {
    wxMessageBox(_("Review Buddy is not available") + " (" + why + ")",
                 _("Review Buddy"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  if (reviewers.size() == 1) {
    // Nothing to choose: start it with the one.
    LaunchReviewBuddy(reviewers.front());
    return;
  }
  wxArrayString names;
  for (const AgentDef &reviewer : reviewers) {
    names.Add(reviewer.name);
  }
  wxSingleChoiceDialog dialog(this, _("Review this session with:"),
                              _("Review Buddy"), names);
  dialog.SetSelection(0);
  if (dialog.ShowModal() != wxID_OK) {
    return;
  }
  // The dialog ran an event loop: the session may have ended or restarted, or
  // another way (the Goto Anything command) may have started a review buddy.
  if (m_review || !SupportsReviewBuddy()) {
    return;
  }
  const int selection = dialog.GetSelection();
  if (selection >= 0 && selection < static_cast<int>(reviewers.size())) {
    // Deferred: leave the dialog first.
    CallAfter(&SessionPage::LaunchReviewBuddy, reviewers[selection]);
  }
}

wxString
SessionPage::GetReviewBuddyUnavailableReason(std::vector<AgentDef> *reviewers) {
  if (!SupportsReviewBuddy()) {
    return _("only an agent session can have one");
  }
  RefreshGitState();
  wxString whyNot;
  if (!CanHaveReviewBuddy(whyNot)) {
    return whyNot;
  }
  std::vector<AgentDef> candidates = GetReviewerCandidates();
  if (candidates.empty()) {
    return _("no agent on this host to review with");
  }
  if (reviewers != nullptr) {
    *reviewers = std::move(candidates);
  }
  return wxString();
}

wxString SessionPage::DescribeReviewBuddy() const {
  return m_review ? m_review->Describe() : wxString();
}

bool SessionPage::IsReviewBuddyLive() const {
  return m_review && (m_review->IsRunning() || m_review->HasStalled());
}

bool SessionPage::CanResendReview() const {
  return m_review && !m_review->IsBusy();
}

void SessionPage::ResendReview() {
  if (m_review) {
    m_review->Resend();
  }
}

void SessionPage::StopReview() {
  if (m_review) {
    m_review->Stop();
  }
}

bool SessionPage::HasLatestReview() const {
  return m_review && !m_review->CommentsPath().empty();
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

Account SessionPage::GetAccount() const {
  if (m_agent && m_agent->IsRemote()) {
    return Account::Remote(m_agent->remoteHost, m_agent->remoteUser,
                           m_session.workingDir);
  }
  if (m_agent && m_agent->IsWSL()) {
    const wxString distro = wsl::DistroOf(m_agent->loginShell);
    // A Linux path, empty (the root of the distribution) when it cannot be
    // resolved.
    return Account::WSL(
        distro,
        wsl::ResolveLinuxPath(distro, m_session.workingDir, wxEmptyString));
  }
  if (m_agent == std::nullopt && !m_session.remoteHost.empty()) {
    // A terminal that logged in to a remote host. The folder it reports is one
    // of the host once the login was typed (m_programStarted); before that it
    // is a folder of the local shell (a Windows one, or an MSYS one that also
    // starts with `/`), and the folder of the session is used.
    const wxString dir = GetCurrentDir();
    return Account::Remote(
        m_session.remoteHost, m_session.remoteUser,
        m_programStarted && dir.StartsWith("/") ? dir : m_session.workingDir);
  }
  if (m_agent == std::nullopt) {
    // A terminal can run a shell of a WSL distribution too.
    if (const wxString distro = wsl::DistroOf(m_session.loginShell);
        !distro.empty()) {
      // The folder of the shell, if it is a Linux path.
      const wxString dir = GetCurrentDir();
      return Account::WSL(distro, dir.StartsWith("/") ? dir : wxString{});
    }
  }
  const wxString dir = GetCurrentDir();
  return Account::LocalHost(::wxDirExists(dir) ? dir : wxString{});
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
  KLOG_DEBUG() << "Launching review buddy '" << reviewer.name << "' for '"
               << m_session.name << "'";

  // The folder as Kennel reaches it (it reads and writes the review files).
  const wxString hostDir = HostWorkingDir();
  if (hostDir.empty() && !m_agent->IsRemote()) {
    ShowNotice(_("Cannot find the session's working directory"));
    return;
  }

  // The reviewer's pane opens when the first request is ready (it is written
  // first, possibly over SSH): the agent reads it as soon as it starts.
  ClearNotice(); // The message about an earlier review
  m_review = std::make_unique<ReviewBuddy>(
      ReviewBuddy::Target{hostDir, m_agent->remoteHost, m_agent->remoteUser,
                          m_session.name},
      m_terminal,
      [this, reviewer](const wxString &prompt, const wxString &folder) {
        return StartReviewer(reviewer, prompt, folder);
      },
      [this](wxTerminalViewCtrl *terminal) { FocusTerminal(terminal); });
  m_review->SetEventTarget(this);
  // Before Begin(): it gets the first event. It shows the review in the header
  // of the agent's terminal until the review buddy is closed.
  m_reviewHeader = std::make_unique<ReviewBuddyHeader>(m_terminal, this);
  m_review->Begin();
}

wxTerminalViewCtrl *SessionPage::StartReviewer(const AgentDef &reviewer,
                                               const wxString &prompt,
                                               const wxString &folder) {
  if (m_reviewTerminal != nullptr || m_terminal == nullptr) {
    return m_reviewTerminal;
  }

  // The reviewer does not run in the folder of the main agent: the tools keep
  // their history (what --continue / --resume pick up) per folder.
  auto inFolder = [&folder](const wxString &base) {
    return base + (base.EndsWith("/") || base.EndsWith("\\") ? "" : "/") +
           folder;
  };
  const wxString reviewerDir = inFolder(
      m_session.workingDir.empty() ? wxString("~") : m_session.workingDir);

  const auto &prefs = AppManager::Get().GetPrefs();
  wxString shellCommand = prefs.terminalLoginShell;
  if (!reviewer.loginShell.empty()) {
    shellCommand = reviewer.loginShell;
  }
  std::optional<wxString> cwd;
  if (!reviewer.IsRemote() && !reviewer.IsWSL() &&
      !m_session.workingDir.empty()) {
    cwd = inFolder(HostWorkingDir());
  }

  // As for the agent's own terminal (CreateTerminal), the environment is the
  // one of the local process: the shell, or the ssh client for a remote agent.
  // BuildCommandLine() exports the same variables on the remote side.
  // A WSL agent's directory is a Linux path: the shell command takes it.
  m_reviewTerminal = TerminalManager::Get().CreateTerminal(
      m_reviewBuddyPanel, {.shellCommand = shellCommand,
                           .workingDir = reviewerDir,
                           .cwd = cwd,
                           .env = reviewer.env});
  AddToPane(m_reviewBuddyPanel, m_reviewTerminal);
  m_reviewAcceleratorInterceptor =
      std::make_unique<AcceleratorInterceptor>(m_reviewTerminal);
  for (const wxString &command :
       BuildCommandLine(reviewer, reviewerDir, false, prompt)) {
    m_reviewTerminal->SendCommand(command);
  }
  ConfigureTerminal(m_reviewTerminal);
  m_reviewTerminal->Bind(wxEVT_TERMINAL_TERMINATED, [this](wxTerminalEvent &e) {
    e.Skip();
    // The reviewer quit: close its pane, as a session's tab closes when its
    // process exits.
    CallAfter(&SessionPage::CloseReviewBuddy);
  });
  // What the reviewer's program is doing shows on the page, like the agent's.
  m_reviewTerminal->Bind(wxEVT_TERMINAL_PROGRAM_STATUS,
                         &SessionPage::OnProgramStatus, this);
  m_reviewerName = reviewer.name;
  m_reviewerWorking = true;
  // After the theme and before the terminal starts, as for the agent's.
  UpdateReviewHeader();

  // Half each. Not the default position (0): wx puts the sash where it was when
  // the splitter was unsplit last, which was at the size of a page that had not
  // been laid out yet.
  m_splitterReviewBuddy->SplitVertically(
      m_agentPanel, m_reviewBuddyPanel,
      (m_splitterReviewBuddy->GetClientSize().x -
       m_splitterReviewBuddy->GetSashSize()) /
          2);
  m_reviewTerminal->EnsureStarted();
  // The reviewer is the active agent now; it may ask for a permission.
  FocusTerminal(m_reviewTerminal);
  return m_reviewTerminal;
}

void SessionPage::CloseReviewBuddy() {
  // Destroyed on return, after the reviewer's pane: it deletes the folder the
  // reviewer ran in.
  const std::unique_ptr<ReviewBuddy> review = std::move(m_review);
  ClearNotice();
  // The header shows the session again.
  m_reviewHeader.reset();
  UpdateHeader(/*force=*/true);
  if (m_reviewTerminal == nullptr) {
    return;
  }
  m_splitterReviewBuddy->Unsplit(m_reviewBuddyPanel);
  DestroyReviewPane();
  // Only the agent's terminal counts again.
  UpdateProgramActivity();
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
    // Fetched every time: the agent rewrites the review file on the host.
    mainView->GetSessionsBook()->OpenRemoteFile(
        RemoteHostDetails{m_agent->remoteHost, m_agent->remoteUser}, relative,
        searchDirs);
    return;
  }
  const wxString hostDir = HostWorkingDir();
  CHECK_NOT_EMPTY_OR_RETURN(hostDir);
  mainView->GetSessionsBook()->OpenLocalFile(
      wxFileName(hostDir, relative).GetFullPath());
}

// ---------------------------------------------------------------------------
// Find
// ---------------------------------------------------------------------------

wxString SessionPage::GetFindWhat() const {
  wxTerminalViewCtrl *terminal = SearchTerminal();
  if (terminal == nullptr) {
    return wxString();
  }
  wxString first;
  if (const auto appSelection = terminal->GetAppSelection()) {
    first = SearchBar::TextFromSelection(*appSelection);
  }
  if (first.empty()) {
    if (const auto selected = terminal->GetMouseSelection()) {
      first = SearchBar::TextFromSelection(*selected);
    }
  }
  return first;
}

SearchTarget SessionPage::GetSearchTarget() const {
  const Account account = GetAccount();
  return {account, account.m_rootDir};
}

void SessionPage::ShowFind() {
  // The selected text of the terminal is what the user looks for.
  const wxString selected = GetFindWhat();
  const wxString text = selected.empty() ? m_findText : selected;
  if (!m_searchBar->IsShown()) {
    m_searchBar->Show();
    Layout();
  }
  m_searchBar->Activate(text);
}

void SessionPage::FindIn(bool forward) {
  wxTerminalViewCtrl *terminal = SearchTerminal();
  if (terminal == nullptr || m_findText.empty()) {
    return;
  }
  // The terminal bar has no options: the case is ignored.
  terminal->FindText(m_findText, (forward ? wxTerminalViewCtrl::kForward
                                          : wxTerminalViewCtrl::kBackward) |
                                     wxTerminalViewCtrl::kCaseInSensitive);
}

void SessionPage::FindNext(bool forward) {
  if (m_findText.empty()) {
    ShowFind();
    return;
  }
  FindIn(forward);
}

void SessionPage::OnSearchBarFind(wxCommandEvent &event) {
  m_findText = event.GetString();
  wxTerminalViewCtrl *terminal = SearchTerminal();
  if (terminal == nullptr) {
    return;
  }
  if (m_findText.empty()) {
    terminal->ClearUserSelection();
    return;
  }
  if (event.GetInt() & SearchBar::kIncremental) {
    // The terminal looks on from the end of its match, so a longer text would
    // skip it. Start again from the bottom, where the newest output is.
    terminal->ClearUserSelection();
    terminal->FindText(m_findText, wxTerminalViewCtrl::kBackward |
                                       wxTerminalViewCtrl::kCaseInSensitive);
    return;
  }
  FindIn((event.GetInt() & SearchBar::kForward) != 0);
}

void SessionPage::OnSearchBarClose(wxCommandEvent &event) {
  wxUnusedVar(event);
  m_searchBar->Hide();
  Layout();
  // The matches stay highlighted otherwise: in both terminals, as the search
  // may have run in either.
  for (wxTerminalViewCtrl *terminal : GetTerminals()) {
    terminal->ClearUserSelection();
  }
  SetFocus();
}
