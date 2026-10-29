#include "MainView.hpp"
#include "app/ToolBarUtils.h"

#include "MainFrame.h"
#include "StartAgentDialog.hpp"
#include "ThemeLoader.h"
#include "ThemeManager.h"
#include "app/AppTheme.h"
#include "app/AssetBootstrap.h"
#include "app/FileExplorerView.hpp"
#include "app/FilePage.hpp"
#include "app/FilesView.hpp"
#include "app/FlatView.hpp"
#include "app/GotoAnythingBar.hpp"
#include "app/PageRegistry.hpp"
#include "app/PageSwitcherDlg.hpp"
#include "app/SessionGroup.h"
#include "app/SessionPage.hpp"
#include "app/TerminalsView.hpp"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/EventNotifier.hpp"
#include "core/JobLog.h"
#include "core/Logger.h"
#include "core/SftpClient.h"
#include "core/Workspace.h"
#include "core/WorkspaceStore.h"
#include "core/WslPath.h"

#include "terminal_view.h"

#include "core/Helpers.h"
#include <algorithm>
#include <thread>
#include <wx/dir.h>
#include <wx/fontdlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/utils.h>
#include <wx/xrc/xmlres.h>

namespace {
// The width the side panes start with when there is no saved layout, in DIP.
constexpr int kSidePaneWidthDip = 300;
// The size of a side pane that is dragged out of the window, in DIP. A saved
// layout has the size the user gave it.
constexpr int kFloatingPaneWidthDip = 400;
constexpr int kFloatingPaneHeightDip = 600;
// The names of the side panes (see MainViewBase).
const wxString kSidePanes[] = {"SessionsPage", "FileBrowser", "Files",
                               "Terminals"};

// What the choice of the file explorer shows for a session: the name, and the
// group when the session is in one.
wxString ExplorerLabel(const SessionRef &ref) {
  return ref.groupName == kTerminalsGroupName
             ? ref.name
             : ref.name + " (" + ref.groupName + ")";
}

// The key of a file page (see FileEvent::MakeKey) for a saved file.
wxString KeyOfFile(const SessionFile &file) {
  return FileEvent::MakeKey(file.path, {file.remoteHost, file.remoteUser});
}

void PushRecent(std::vector<wxString> &list, const wxString &value,
                size_t maxSize = 10) {
  if (value.empty()) {
    return;
  }
  list.erase(std::remove(list.begin(), list.end(), value), list.end());
  list.insert(list.begin(), value);
  if (list.size() > maxSize) {
    list.resize(maxSize);
  }
}

// The name of `page` (a SessionPage or a FilePage), see SessionRef.
std::optional<SessionRef> RefOf(wxWindow *page) {
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    return SessionRef{session->GetSession().groupName,
                      session->GetSession().name};
  }
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    return SessionRef{kFilesGroupName, file->GetKey()};
  }
  return std::nullopt;
}
} // namespace

MainView::MainView(wxWindow *parent)
    : MainViewBase(parent), m_registry(&AppManager::Get().Adapters()),
      m_workspaceStore(&AppManager::Get().Workspace()),
      m_paths(AppManager::Get().Paths()) {

  const auto &prefs = AppManager::Get().GetPrefs();
  auto &themeMgr = ThemeManager::Get();
  {
    auto themes = LoadShippedThemes();
    if (themes.empty()) {
      KLOG_ERROR() << "Broken installation! can not find themes!";
      std::exit(1);
    }

    wxFont font;
    font.SetNativeFontInfo(prefs.terminalFontDesc);
    for (auto &t : themes) {
      t.theme.font = font;
    }

    themeMgr.Initialize(std::move(themes), prefs.terminalTheme);
    themeMgr.SetBlockCursor(prefs.blockCursor);
  }

  LoadBitmaps();

  m_pages = std::make_unique<PageRegistry>();
  m_flatView = new FlatView(GetPanelFlatView());
  GetPanelFlatView()->GetSizer()->Add(m_flatView, wxSizerFlags(1).Expand());
  m_filesView = new FilesView(GetPanelFiles());
  GetPanelFiles()->GetSizer()->Add(m_filesView, wxSizerFlags(1).Expand());
  m_terminalsView = new TerminalsView(GetPanelTerminals());
  GetPanelTerminals()->GetSizer()->Add(m_terminalsView,
                                       wxSizerFlags(1).Expand());
  m_fileExplorer = new FileExplorerView(GetPanelFileExplorer());
  GetPanelFileExplorer()->GetSizer()->Add(m_fileExplorer,
                                          wxSizerFlags(1).Expand());
  // The size a pane gets when it floats (it only matters then, so no Update()).
  for (const wxString &name : kSidePanes) {
    m_auimgr->GetPane(name).FloatingSize(
        FromDIP(wxSize(kFloatingPaneWidthDip, kFloatingPaneHeightDip)));
  }
  // Above the pages, in the sizer of the centre; hidden until it is asked for.
  m_gotoBar = new GotoAnythingBar(m_mainPanel);
  m_mainPanel->GetSizer()->Insert(0, m_gotoBar, wxSizerFlags(0).Expand());
  m_gotoBar->Hide();

  for (int i = 0; i < kSpinnerFrameCount; ++i) {
    wxString name;
    name.Printf("spinner-%d.svg", i);
    const wxString path = ResolveIconPath(name);
    if (!path.empty() && wxFileName::FileExists(path)) {
      m_spinnerFrames[i] = wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
    }
  }
  if (const wxString path = ResolveIconPath("hand.svg");
      !path.empty() && wxFileName::FileExists(path)) {
    m_handIcon = wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
  }
  if (const wxString path = ResolveIconPath("check.svg");
      !path.empty() && wxFileName::FileExists(path)) {
    m_checkIcon = wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
  }

  EventNotifier::Get()->Bind(wxEVT_PAGEVIEW_SELECTED, &MainView::OnPageSelected,
                             this);
  Bind(wxEVT_PAGEVIEW_MENU, &MainView::OnPageMenu, this);
  Bind(wxEVT_FILETREE_ACTIVATED, &MainView::OnFileTreeActivated, this);
  Bind(wxEVT_FILEEXPLORER_SESSION_SELECTED, &MainView::OnExplorerSession, this);
  Bind(wxEVT_GOTOANYTHING_CHANGED, &MainView::OnGotoChanged, this);
  Bind(wxEVT_GOTOANYTHING_CLOSED, &MainView::OnGotoClosed, this);
  Bind(wxEVT_SESSION_IDLE, &MainView::OnSessionIdle, this);
  Bind(wxEVT_PAGE_SWITCHER_DONE, &MainView::OnPageSwitcherDone, this);
  Bind(wxEVT_SESSION_ACTIVE, &MainView::OnSessionActive, this);
  Bind(wxEVT_SESSION_EXITED, &MainView::OnSessionExited, this);
  Bind(wxEVT_SESSION_PROGRAM, &MainView::OnSessionProgram, this);
  Bind(wxEVT_SESSION_CWD, &MainView::OnSessionCwd, this);
  Bind(wxEVT_TIMER, &MainView::OnSpinnerTimer, this, m_spinnerTimer.GetId());
  Bind(wxEVT_IDLE, &MainView::OnIdleEvent, this);
  // A FilePage handles its own save events first and then Skip()s them, so
  // they propagate up to wxTheApp, where MainView does its part (activity
  // indicator, closing after a save).
  wxTheApp->Bind(wxEVT_FILE_SAVE_STARTED, &MainView::OnFileSaveStarted, this);
  wxTheApp->Bind(wxEVT_FILE_SAVE_DONE, &MainView::OnFileSaveDone, this);
}

MainView::~MainView() {
  EventNotifier::Get()->Unbind(wxEVT_PAGEVIEW_SELECTED,
                               &MainView::OnPageSelected, this);
  Unbind(wxEVT_PAGEVIEW_MENU, &MainView::OnPageMenu, this);
  Unbind(wxEVT_SESSION_IDLE, &MainView::OnSessionIdle, this);
  Unbind(wxEVT_SESSION_ACTIVE, &MainView::OnSessionActive, this);
  Unbind(wxEVT_SESSION_EXITED, &MainView::OnSessionExited, this);
  Unbind(wxEVT_SESSION_PROGRAM, &MainView::OnSessionProgram, this);
  Unbind(wxEVT_SESSION_CWD, &MainView::OnSessionCwd, this);
  m_spinnerTimer.Stop();
  Unbind(wxEVT_TIMER, &MainView::OnSpinnerTimer, this, m_spinnerTimer.GetId());
  Unbind(wxEVT_IDLE, &MainView::OnIdleEvent, this);
  if (wxTheApp) {
    wxTheApp->Unbind(wxEVT_FILE_SAVE_STARTED, &MainView::OnFileSaveStarted,
                     this);
    wxTheApp->Unbind(wxEVT_FILE_SAVE_DONE, &MainView::OnFileSaveDone, this);
  }
}

// ---------------------------------------------------------------------------
// The two views
// ---------------------------------------------------------------------------

void MainView::OnPageSelected(PageViewEvent &event) {
  event.Skip();
  if (event.GetEventObject() == this) {
    return; // Sent by ActivatePage(), the page is already showing.
  }
  // The page may have been closed while the event was pending.
  auto *page = m_pages->FindPage(event.GetRef());
  if (page == nullptr || page == m_sessionsBook->GetCurrentPage()) {
    return;
  }
  // The sender has selected it already, and the other views follow the same
  // event: no need to tell them.
  ShowPage(page, event.UpdateRecent(), false);
}

void MainView::OnExplorerSession(PageViewEvent &event) {
  // The user chose a session in the file explorer: show it, as a pick in the
  // list of the sessions does. The views follow.
  auto *page = m_pages->FindPage(event.GetRef());
  if (page != nullptr && page != m_sessionsBook->GetCurrentPage()) {
    ShowPage(page);
  }
}

void MainView::OnFileTreeActivated(FileEvent &event) {
  if (event.IsDir()) {
    return; // The tree opens or closes the folder
  }
  const Account &account = event.GetAccount();
  if (account.IsRemote()) {
    OpenRemoteFile(RemoteHostDetails{account.m_name, account.m_user},
                   event.GetPath(), {});
  } else if (account.IsWSL()) {
    OpenLocalFile(wsl::ToWindowsPath(account.m_name, event.GetPath()));
  } else {
    OpenLocalFile(event.GetPath());
  }
}

void MainView::OnPageMenu(PageViewEvent &event) {
  auto *on = dynamic_cast<wxWindow *>(event.GetEventObject());
  CHECK_NOT_NULL_RETURN(on);
  if (!event.GetSessionName().empty()) {
    if (auto *page = m_pages->FindPage(event.GetRef())) {
      ShowPageMenu(page, on);
    }
  } else {
    ShowBackgroundMenu(on);
  }
}

void MainView::RefreshViews() {
  if (m_viewsRefreshPending) {
    return;
  }
  m_viewsRefreshPending = true;
  CallAfter(&MainView::DoRefreshViews);
}

void MainView::DoRefreshViews() {
  m_viewsRefreshPending = false;
  // Group order; the list of the sessions sorts it if the user picked a column.
  const auto pages = m_pages->GetPages();
  const auto active = GetActivePageRef();
  m_flatView->SetPages(pages, active);
  m_filesView->SetPages(pages, active);
  m_terminalsView->SetPages(pages, active);
}

std::vector<PageInfo>
MainView::GetPagesByRecency(const std::optional<SessionRef> &first) const {
  std::vector<PageInfo> remaining = m_pages->GetPages();
  std::vector<PageInfo> ordered;
  auto take = [&](const SessionRef &ref) {
    auto it = std::find_if(
        remaining.begin(), remaining.end(),
        [&ref](const PageInfo &info) { return info.Ref() == ref; });
    if (it != remaining.end()) {
      ordered.push_back(std::move(*it));
      remaining.erase(it);
    }
  };

  if (first) {
    take(*first);
  }
  for (const auto &ref : m_recent) {
    take(ref);
  }
  ordered.insert(ordered.end(), std::make_move_iterator(remaining.begin()),
                 std::make_move_iterator(remaining.end()));
  return ordered;
}

void MainView::TouchRecent(const SessionRef &ref) {
  if (!m_recent.empty() && m_recent.front() == ref) {
    return;
  }
  // Drop the closed pages while at it, so the list cannot grow forever.
  std::erase_if(m_recent, [&](const SessionRef &r) {
    return r == ref || m_pages->FindPage(r) == nullptr;
  });
  m_recent.insert(m_recent.begin(), ref);
  SyncWorkspaceSoon();
}

void MainView::RenameRecent(const SessionRef &from, const SessionRef &to) {
  m_fileExplorer->RenameSession(from, to, ExplorerLabel(to));
  std::replace(m_recent.begin(), m_recent.end(), from, to);
}

void MainView::SyncWorkspaceSoon() {
  if (m_syncPending) {
    return;
  }
  m_syncPending = true;
  CallAfter([this] {
    m_syncPending = false;
    SyncWorkspaceToDisk();
  });
}

void MainView::SyncWorkspaceToDisk() {
  Workspace ws;
  ws.version = 1;
  for (auto *group : m_pages->GetGroups()) {
    if (!group->IsTerminalsGroup() && !group->IsDefaultGroup() &&
        !group->GetIcon().empty()) {
      ws.groups.push_back(GroupMeta{group->GetGroupName(), group->GetIcon()});
    }
    for (auto *page : m_pages->GetGroupSessions(group->GetGroupName())) {
      Session session = page->GetSession();
      if (session.plainTerminal) {
        // Where the user is now, if it is a folder of this machine (a shell in
        // WSL, for one, reports a path that is not).
        const wxString dir = page->GetCurrentDir();
        if (!dir.empty() && wxDirExists(dir)) {
          session.workingDir = dir;
        }
      }
      ws.sessions.push_back(std::move(session));
    }
  }

  for (auto *file : m_pages->GetFilePages()) {
    const auto &host = file->GetRemoteHostDetails();
    ws.files.push_back(SessionFile{file->GetPath(), host.host, host.user});
  }
  // The remote files that are still coming back from the last run.
  ws.files.insert(ws.files.end(), m_restoringFiles.begin(),
                  m_restoringFiles.end());
  // In the order of the last run (new files last): the remote files come back
  // in the order the fetches finish.
  {
    std::map<wxString, size_t> rank; // The key of a file -> its saved place
    for (const SessionFile &file : AppManager::Get().InitialWorkspace().files) {
      rank.emplace(KeyOfFile(file), rank.size());
    }
    const auto rankOf = [&rank](const SessionFile &file) {
      const auto it = rank.find(KeyOfFile(file));
      return it == rank.end() ? rank.size() : it->second;
    };
    std::stable_sort(ws.files.begin(), ws.files.end(),
                     [&rankOf](const SessionFile &a, const SessionFile &b) {
                       return rankOf(a) < rankOf(b);
                     });
  }

  for (const auto &info : GetPagesByRecency(std::nullopt)) {
    auto *page = m_pages->FindPage(info.Ref());
    auto *session = dynamic_cast<SessionPage *>(page);
    if (session && session->GetSession().IsPersistent()) {
      ws.recentSessions.push_back(SessionRef{session->GetSession().groupName,
                                             session->GetSession().name});
    } else if (dynamic_cast<FilePage *>(page) != nullptr) {
      ws.recentSessions.push_back(info.Ref());
    }
  }

  if (Status st = m_workspaceStore->Save(ws); !st.ok()) {
    KLOG_WARN() << "Could not persist workspace.json: " << st.message();
  }
}

// ---------------------------------------------------------------------------
// Showing pages
// ---------------------------------------------------------------------------

void MainView::ShowPage(wxWindow *page, bool updateRecent, bool notifyViews) {
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    SelectSessionPage(session, updateRecent, notifyViews);
  } else if (auto *file = dynamic_cast<FilePage *>(page)) {
    SelectFilePage(file, updateRecent, notifyViews);
  }
}

void MainView::ActivatePage(wxWindow *page, bool updateRecent,
                            bool notifyViews) {
  auto ref = RefOf(page);
  if (!ref) {
    return;
  }

  int where = m_sessionsBook->FindPage(page);
  if (where != wxNOT_FOUND) {
    m_sessionsBook->SetSelection(where);
  }
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    if (auto *group = m_pages->GetGroup(session->GetSession().groupName)) {
      group->SetLastActive(session->GetSession().name);
    }
    m_fileExplorer->SetActiveSession(*ref);
    // The user sees the page now: the check mark goes away.
    if (session->GetProgramActivity() == SessionPage::ProgramActivity::Done) {
      session->AcknowledgeDone();
      ApplyPageIcon(session);
    }
  } else if (auto *file = dynamic_cast<FilePage *>(page)) {
    // The file is selected in the tree of its account, if that is showing.
    m_fileExplorer->RevealFile(file->GetAccount(), file->GetAccountPath());
  }

  if (notifyViews) {
    PageViewEvent evtSelected(wxEVT_PAGEVIEW_SELECTED);
    evtSelected.SetEventObject(this);
    evtSelected.SetUpdateRecent(updateRecent);
    evtSelected.SetRef(*ref);
    EventNotifier::Get()->AddPendingEvent(evtSelected);
  }
  if (updateRecent) {
    TouchRecent(*ref);
  }
}

void MainView::SelectSessionPage(SessionPage *page, bool updateRecent,
                                 bool notifyViews) {
  CHECK_NOT_NULL_RETURN(page);
  ActivatePage(page, updateRecent, notifyViews);
  page->CallAfter(&SessionPage::SetFocus);
  page->ApplyTitle();
  page->PublishStatus();
}

void MainView::SelectFilePage(FilePage *page, bool updateRecent,
                              bool notifyViews) {
  CHECK_NOT_NULL_RETURN(page);
  ActivatePage(page, updateRecent, notifyViews);
  page->CallAfter(&FilePage::FocusEditor);
  wxTheApp->GetTopWindow()->SetLabel(page->GetPath());
  GetMainFrame()->ClearSessionStatus();
  page->PublishStatus();
}

void MainView::ActivateGroup(const wxString &groupName) {
  ShowPage(m_pages->GetGroupDefaultPage(groupName));
}

void MainView::SelectFallbackPage(const wxString &preferredGroup) {
  ShowPage(m_pages->GetFallbackPage(preferredGroup));
}

void MainView::SwitchPage(bool forward) {
  if (m_switcher) {
    m_switcher->Advance(forward);
    return;
  }
  const auto ordered = GetPagesByRecency(GetActivePageRef());
  if (ordered.size() <= 1) {
    return;
  }

  // With Ctrl already released (e.g. picked from the menu) there is nothing to
  // hold on to, so skip the popup.
  if (wxGetMouseState().RawControlDown()) {
    std::vector<PageSwitcherItem> items;
    for (const auto &info : ordered) {
      items.push_back({info.label, info.dir, info.group, info.DisplayIcon()});
    }
    // Modeless on purpose: a modal loop left the app lagging after closing.
    m_switcherPages = ordered;
    m_switcher =
        new PageSwitcherDlg(wxGetTopLevelParent(this), this, items, forward);
    return;
  }
  const int count = static_cast<int>(ordered.size());
  ShowPage(m_pages->FindPage(ordered[forward ? 1 : count - 1].Ref()));
}

void MainView::OnPageSwitcherDone(wxCommandEvent &event) {
  const int chosen = event.GetInt();
  auto pages = std::move(m_switcherPages);
  m_switcherPages.clear();
  if (m_switcher) {
    m_switcher->Destroy();
    m_switcher = nullptr;
  }
  if (chosen < 0 || chosen >= static_cast<int>(pages.size())) {
    return;
  }
  ShowPage(m_pages->FindPage(pages[chosen].Ref()));
}

// ---------------------------------------------------------------------------
// Adding / removing pages
// ---------------------------------------------------------------------------

SessionPage *MainView::AddSession(SessionPage *page) {
  if (!m_pages->AddSession(page)) {
    return nullptr;
  }
  m_sessionsBook->AddPage(page, page->GetSession().name, false);
  const SessionRef ref{page->GetSession().groupName, page->GetSession().name};
  // Sessions only: a plain terminal has no tree.
  if (!page->IsPlainTerminal()) {
    m_fileExplorer->AddSession(ref, ExplorerLabel(ref), page->GetAccount());
  }
  RefreshViews();
  return page;
}

SessionPage *MainView::AddSessionPage(const Session &session, bool resume) {
  auto *group = m_pages->EnsureGroup(session.groupName);
  if (group == nullptr) {
    return nullptr;
  }

  std::optional<AgentDef> agent{std::nullopt};
  if (group->IsSessionGroup() && !session.agentName.empty()) {
    auto &registry = AppManager::Get().Adapters();
    const AgentDef *pagent = registry.FindAgent(session.agentName);
    if (pagent == nullptr) {
      KLOG_ERROR() << wxString::Format("No such agent: %s", session.agentName);
      return nullptr;
    }
    agent = *pagent;
  }

  auto *page = new SessionPage(m_sessionsBook, agent, session, resume);
  if (page->Status() == SessionStatus::Starting) {
    // Could not start the session
    wxDELETE(page);
    return nullptr;
  }

  if (AddSession(page) == nullptr) {
    wxDELETE(page);
    return nullptr;
  }
  return page;
}

void MainView::AddFilePage(FilePage *page) {
  m_sessionsBook->AddPage(page, page->GetDisplayName(), false);
  m_pages->AddFile(page);
  RefreshViews();
  SyncWorkspaceSoon();
}

void MainView::RemovePage(wxWindow *page) {
  const bool wasActive = (m_sessionsBook->GetCurrentPage() == page);

  // Prefer a sibling of a closed session. Sessions are looked at by group
  // name since the group itself may disappear with its last session.
  wxString preferredGroup;
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    const wxString &groupName = session->GetSession().groupName;
    if (m_pages->GetGroupSessions(groupName).size() > 1) {
      preferredGroup = groupName;
    }
  }

  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    m_fileExplorer->RemoveSession(SessionRef{session->GetSession().groupName,
                                             session->GetSession().name});
  }
  m_pages->RemovePage(page);
  int where = m_sessionsBook->FindPage(page);
  if (where != wxNOT_FOUND) {
    m_sessionsBook->DeletePage(where); // destroys the page window
  }
  // The status bar showed this page: it keeps showing nothing if no other page
  // takes over (the fallback below publishes its own status).
  if (wasActive) {
    GetMainFrame()->ClearSessionStatus();
  }
  RefreshViews();

  // Deferred: the page may be removed from inside one of its own handlers or
  // a menu callback (see OnSessionExited for the same issue).
  if (wasActive) {
    CallAfter(&MainView::SelectFallbackPage, preferredGroup);
  }
  // Cleanup the empty groups
  CallAfter(&MainView::RemoveEmptyGroups);
}

void MainView::RemoveEmptyGroups() { m_pages->RemoveEmptyGroups(); }

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

void MainView::OnFileSaveStarted(FileEvent &e) {
  auto *page = m_pages->FindFile(e.GetKey());
  const wxString name = page ? page->GetDisplayName() : e.GetPath();
  GetMainFrame()->SetActivityText(wxString::Format(_("Saving %s"), name));
  GetMainFrame()->StartActivityIndicator();
}

void MainView::OnFileSaveDone(FileEvent &e) {
  GetMainFrame()->StopActivityIndicator();
  GetMainFrame()->ClearActivityText();

  const wxString key = e.GetKey();
  if (m_closeAfterSave.erase(key) > 0 && e.GetStatusCode().ok()) {
    // Deferred: this event is still being dispatched by the very page that is
    // about to be destroyed.
    CallAfter(&MainView::CloseFileByKey, key);
  }
}

void MainView::OpenRemoteFile(const RemoteHostDetails &remoteHost,
                              const wxString &clickedPath,
                              const std::vector<wxString> &searchDirs, int line,
                              int column) {
  KLOG_DEBUG() << "Opening remote file: " << clickedPath;
  const wxString key = FileEvent::MakeKey(clickedPath, remoteHost);
  if (line > 0) {
    m_gotoAfterFetch[key] = FileLocation{clickedPath, line, column};
  }
  if (!m_fetchingRemote.insert(key).second) {
    // A previous click, or the restore of the file, is still
    // connecting/downloading. The file is shown and selected when it arrives,
    // as the user asked for it now.
    // The file stays in m_restoringFiles until the result is in.
    if (IsRestoringFile(key)) {
      m_selectWhenRestored.insert(key);
    }
    m_ignoreRemote.erase(key);
    return;
  }

  // The worker gets copies of the data it needs and never touches the UI. It
  // hands its result back by value through CallAfter(), which runs
  // OnRemoteFileRead() on the UI thread.
  std::thread([this, remoteHost, clickedPath, searchDirs]() {
    auto read = SftpClient::ReadFile(remoteHost.host, remoteHost.user,
                                     clickedPath, searchDirs);

    RemoteReadResult result;
    result.remoteHost = remoteHost;
    result.clickedPath = clickedPath;
    result.ok = read.ok();
    if (read.ok()) {
      result.path = read.value().path;
      result.content = std::move(read.value().content);
    } else {
      result.error = read.status().message();
    }
    CallAfter(&MainView::OnRemoteFileRead, result);
  }).detach();
}

void MainView::ReloadRemoteFile(const RemoteHostDetails &remoteHost,
                                const wxString &path) {
  const wxString key = FileEvent::MakeKey(path, remoteHost);
  if (m_fetchingRemote.count(key) != 0) {
    return; // Already being fetched.
  }
  m_reloadingRemote.insert(key);
  OpenRemoteFile(remoteHost, path, {});
}

bool MainView::IsRestoringFile(const wxString &key) const {
  return std::any_of(
      m_restoringFiles.begin(), m_restoringFiles.end(),
      [&key](const SessionFile &file) { return KeyOfFile(file) == key; });
}

bool MainView::TakeRestoringFile(const wxString &key) {
  return std::erase_if(m_restoringFiles, [&](const SessionFile &f) {
           return KeyOfFile(f) == key;
         }) != 0;
}

void MainView::OnRemoteFileRead(const RemoteReadResult &result) {
  const wxString fetchKey =
      FileEvent::MakeKey(result.clickedPath, result.remoteHost);
  m_fetchingRemote.erase(fetchKey);
  std::optional<FileLocation> target;
  if (auto node = m_gotoAfterFetch.extract(fetchKey)) {
    target = std::move(node.mapped());
  }
  const bool reload = m_reloadingRemote.erase(fetchKey) != 0;
  if (m_ignoreRemote.erase(fetchKey) != 0) {
    return; // The workspace was cleared since
  }
  // A restore that the user asked for in the meantime is a plain open.
  const bool wanted = m_selectWhenRestored.erase(fetchKey) != 0;
  const bool taken = TakeRestoringFile(fetchKey);
  const bool restoring = taken && !wanted;

  if (!result.ok) {
    KLOG_WARN() << "Remote open failed for '" << result.clickedPath
                << "': " << result.error;
    if (taken) {
      SyncWorkspaceSoon(); // The file is not back: drop it from the workspace
    }
    if (!restoring) {
      ::wxMessageBox(result.error, "Kennel", wxICON_WARNING | wxOK, this);
    }
    return;
  }

  const wxString &path = result.path;
  if (result.content.find('\0') != std::string::npos) {
    if (taken) {
      SyncWorkspaceSoon();
    }
    if (restoring) {
      KLOG_WARN() << path << " is a binary file; not restored";
    } else {
      ::wxMessageBox(wxString::Format(_("%s is a binary file."), path),
                     "Kennel", wxICON_INFORMATION | wxOK, this);
    }
    return;
  }

  // Only a file that decodes as UTF-8 is editable: re-encoding anything else
  // on save would corrupt it, so show that read-only.
  bool editable = true;
  wxString text = wxString::FromUTF8(result.content);
  if (text.empty() && !result.content.empty()) {
    text = wxString::From8BitData(result.content.data(), result.content.size());
    editable = false;
  }
  ShowRemoteFile(result.remoteHost, path, text, editable, reload, restoring);
  if (target) {
    if (auto *page =
            m_pages->FindFile(FileEvent::MakeKey(path, result.remoteHost))) {
      page->GotoLine(target->line, target->column);
    }
  }
}

void MainView::OpenLocalFile(const wxString &path, bool restoring) {
  const wxString key = wxFileName(path).GetFullPath();
  if (auto *existing = m_pages->FindFile(key)) {
    if (!restoring) {
      SelectFilePage(existing);
    }
    return;
  }

  auto *page = new FilePage(
      m_sessionsBook, key,
      ThemeManager::Get().ActiveTheme().value_or(wxTerminalTheme{}));
  if (!page->LoadLocal()) {
    page->Destroy();
    if (restoring) {
      KLOG_WARN() << "Could not restore " << key;
    } else {
      wxMessageBox(wxString::Format(_("Could not open %s"), key), "Kennel",
                   wxOK | wxICON_ERROR, this);
    }
    return;
  }
  AddFilePage(page);
  if (!restoring) {
    SelectFilePage(page);
  }
}

void MainView::OpenFileAt(const FileLocation &where) {
  OpenLocalFile(where.path);
  if (where.line <= 0) {
    return;
  }
  if (auto *page = m_pages->FindFile(wxFileName(where.path).GetFullPath())) {
    page->GotoLine(where.line, where.column);
  }
}

void MainView::ShowRemoteFile(const RemoteHostDetails &remoteHost,
                              const wxString &path, const wxString &text,
                              bool editable, bool reload, bool restoring) {
  const wxString key = FileEvent::MakeKey(path, remoteHost);
  if (auto *existing = m_pages->FindFile(key)) {
    // The remote copy may have changed, but only a reload (the user agreed)
    // replaces unsaved edits.
    if (reload || !existing->IsModified()) {
      existing->LoadRemote(text, editable);
    }
    if (!reload && !restoring) {
      SelectFilePage(existing);
    }
    return;
  }

  auto *page = new FilePage(
      m_sessionsBook, path,
      ThemeManager::Get().ActiveTheme().value_or(wxTerminalTheme{}),
      remoteHost);
  page->LoadRemote(text, editable);
  AddFilePage(page);
  if (!restoring) {
    SelectFilePage(page);
  }
}

void MainView::CloseFile(FilePage *page) {
  CHECK_NOT_NULL_RETURN(page);
  if (page->IsSaving()) {
    return; // Let the upload finish first; closing now would lose the result.
  }
  if (page->IsModified() && page->CanSave()) {
    const int answer = wxMessageBox(
        wxString::Format(_("Save changes to %s?"), page->GetDisplayName()),
        "Kennel", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (answer == wxCANCEL) {
      return;
    }
    if (answer == wxYES) {
      if (page->IsRemote()) {
        // Uploads in the background; OnFileSaveDone closes it once it has
        // gone through.
        if (page->SaveAsync()) {
          m_closeAfterSave.insert(page->GetKey());
        }
        return;
      }
      if (!page->Save()) {
        return;
      }
    }
  }

  m_closeAfterSave.erase(page->GetKey());
  RemovePage(page);
  SyncWorkspaceSoon();
}

void MainView::CloseFileByKey(const wxString &key) {
  if (auto *page = m_pages->FindFile(key)) {
    CloseFile(page);
  }
}

void MainView::CloseAllFiles() {
  std::vector<wxString> keys;
  for (auto *page : m_pages->GetFilePages()) {
    keys.push_back(page->GetKey());
  }
  for (const wxString &key : keys) {
    CloseFileByKey(key);
  }
}

// ---------------------------------------------------------------------------
// Launching sessions
// ---------------------------------------------------------------------------

void MainView::StartTerminal() {
  // Restored terminals keep their names: skip the ones that are taken.
  static int terminalId{0};
  wxString name;
  do {
    name = wxString::Format(_("Terminal %d"), ++terminalId);
  } while (IsNameExist(name, kTerminalsGroupName));
  const auto &prefs = AppManager::Get().GetPrefs();
  NewSessionRequest request{
      .name = name,
      .agentName = _("Terminals"), // Fake name
      .workingDir = prefs.terminalHomeDir,
      .groupName = kTerminalsGroupName,
      .plainTerminal = true,
  };
  LaunchSession(request);
}

void MainView::RunJob(const JobDef &job, bool selectAfterLaunch) {
  int &sequence = m_jobRunCounters[job.name];
  wxString candidate;
  do {
    candidate = wxString::Format("%s #%d", job.name, ++sequence);
  } while (IsNameExist(candidate, _("Jobs")));

  NewSessionRequest request{
      .name = candidate,
      .groupName = _("Jobs"),
      .jobName = job.name,
  };

  const wxString trigger = selectAfterLaunch ? "manual" : "scheduled";
  const wxString typeStr = JobTypeToString(job.type);
  const wxString typeLabel =
      job.type == JobType::kPrompt ? "Prompt" : "Command";

  if (job.type == JobType::kPrompt) {
    const AgentDef *agent =
        AppManager::Get().Adapters().FindAgent(job.agentName);
    if (agent == nullptr) {
      KLOG_WARN() << "Job '" << job.name << "': agent '" << job.agentName
                  << "' no longer exists; skipping run";
      AppendJobLogEntry(JobLogEntry{
          .event = "failed",
          .job = job.name,
          .type = typeStr,
          .trigger = trigger,
          .reason = wxString::Format("agent '%s' not found", job.agentName),
          .message =
              wxString::Format("Job '%s' failed to start: agent '%s' not found",
                               job.name, job.agentName),
      });
      return;
    }
    request.agentName = job.agentName;
    request.jobCommands =
        BuildJobCommandLine(*agent, wxEmptyString, job.prompt);
  } else {
    request.jobCommands = {job.command};
  }

  if (!job.keepTerminalOpen) {
    request.jobCommands.push_back("exit");
  }

  AppendJobLogEntry(JobLogEntry{
      .event = "start",
      .job = job.name,
      .type = typeStr,
      .trigger = trigger,
      .session = candidate,
      .message = wxString::Format("Job '%s' started (%s, %s)", job.name,
                                  typeLabel, trigger),
  });

  LaunchSession(request, selectAfterLaunch);
}

void MainView::StartAgent(const wxString &agentName,
                          const wxString &groupName) {
  StartAgentDialog dlg(this);
  if (!agentName.empty()) {
    dlg.SetSelectedClientName(agentName);
  }

  wxString selectedGroupName{groupName};
  if (selectedGroupName.empty() && GetSelectedGroup()) {
    selectedGroupName = GetSelectedGroup()->GetGroupName();
  }

  if (!selectedGroupName.empty()) {
    dlg.SetSelectedGroup(selectedGroupName);
  }

  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  LaunchSession(dlg.GetRequest());
}

bool MainView::LaunchSession(const NewSessionRequest &req,
                             bool selectAfterLaunch) {
  if (req.name.empty()) {
    wxMessageBox("Session name must not be empty", "Launch failed",
                 wxOK | wxICON_ERROR, this);
    return false;
  }
  if (IsNameExist(req.name, req.groupName)) {
    wxMessageBox(
        wxString::Format("A session named '%s' already exists in group '%s'",
                         req.name, req.groupName),
        "Launch failed", wxOK | wxICON_ERROR, this);
    return false;
  }

  Session session;
  session.name = req.name;
  session.agentName = req.agentName;
  session.workingDir = req.workingDir;
  session.groupName = req.groupName;
  session.plainTerminal = req.plainTerminal;
  session.jobCommands = req.jobCommands;
  session.jobName = req.jobName;

  auto *page = AddSessionPage(session, req.resume);
  if (page == nullptr) {
    KLOG_INFO() << "Session creation failed";
    RemoveEmptyGroups();
    return false;
  }

  if (selectAfterLaunch) {
    SelectSessionPage(page);
  }

  SyncWorkspaceToDisk();

  auto &prefs = AppManager::Get().GetPrefs();
  PushRecent(prefs.recentWorkingDirs, req.workingDir);
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not persist recent working dirs: " << st.message();
  }
  return true;
}

// ---------------------------------------------------------------------------
// The left pane
// ---------------------------------------------------------------------------

void MainView::RestoreLayout() {
  // Deferred: the docks can only get the size they had where the window is big
  // enough for them, and the window is not at its final size (e.g. maximized)
  // before the first pass of the event loop.
  CallAfter([this] {
    const wxString &layout = AppManager::Get().GetPrefs().auiLayout;
    bool restored = false;
    if (!layout.empty()) {
      restored = m_auimgr->LoadPerspective(layout, /*update=*/true);
      if (!restored) {
        KLOG_WARN() << "Could not restore the layout of the panes";
      }
    }
    if (!restored) {
      ApplyDefaultPaneWidths();
    }
    m_layoutRestored = true;
  });
}

void MainView::ApplyDefaultPaneWidths() {
  // A dock keeps the size it got when it was created (and a new dock is at most
  // 30% of the window wide), so BestSize() cannot change it any more. A minimum
  // size makes the dock at least that wide; it is dropped again so the user can
  // make the pane narrower.
  const int width = FromDIP(kSidePaneWidthDip);
  for (const wxString &name : kSidePanes) {
    m_auimgr->GetPane(name).BestSize(width, -1).MinSize(width, -1);
  }
  m_auimgr->Update();
  for (const wxString &name : kSidePanes) {
    m_auimgr->GetPane(name).MinSize(wxDefaultSize);
  }
  m_auimgr->Update();
}

void MainView::SaveLayout() {
  // Before the saved layout was applied the panes still hold their defaults,
  // which must not replace what was saved (see RestoreLayout()).
  if (m_layoutRestored) {
    AppManager::Get().GetPrefs().auiLayout = m_auimgr->SavePerspective();
  }
}

void MainView::RestoreSessions() {
  const auto &initial = AppManager::Get().InitialWorkspace();
  if (initial.sessions.empty() && initial.files.empty()) {
    return;
  }

  int restored = 0;
  for (const Session &s : initial.sessions) {
    auto *page = AddSessionPage(s, true);
    if (page) {
      page->GetTerminal()->EnsureStarted();
      ++restored;
    }
  }
  KLOG_INFO() << "Restored " << restored << " session(s)";

  // The files, in the background: the page used last is shown below.
  for (const SessionFile &file : initial.files) {
    if (file.remoteHost.empty()) {
      OpenLocalFile(file.path, /*restoring=*/true);
    } else {
      m_restoringFiles.push_back(file);
      OpenRemoteFile({file.remoteHost, file.remoteUser}, file.path, {});
    }
  }

  // Bring back the recent order and show the page used last.
  m_recent.clear();
  wxWindow *lastUsed = nullptr;
  for (const SessionRef &r : initial.recentSessions) {
    if (auto *page = m_pages->FindPage(r)) {
      m_recent.push_back(r);
      if (lastUsed == nullptr) {
        lastUsed = page;
      }
    }
  }
  if (lastUsed != nullptr) {
    ShowPage(lastUsed);
  } else if (m_pages->GroupCount() > 0) {
    ActivateGroup(m_pages->GetFirstGroupName());
  }

  // Anything that failed to restore into the UI (e.g. its agent no longer
  // exists) must not be written back: the UI is the only source of truth (but
  // for the remote files that are still coming back, see m_restoringFiles).
  SyncWorkspaceToDisk();
}

// ---------------------------------------------------------------------------
// Theme, font, prefs
// ---------------------------------------------------------------------------

void MainView::ApplyFont(const wxFont &f) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetFont(f);
  if (!active) {
    return;
  }
  for (auto *page : m_pages->GetAllSessions()) {
    page->ApplyTheme(*active);
    for (auto *terminal : page->GetTerminals()) {
      terminal->SendSizeEvent();
    }
  }
  for (auto *filePage : m_pages->GetFilePages()) {
    filePage->ApplyTheme(*active);
  }
  m_sessionsBook->SendSizeEvent();
  KLOG_INFO() << "Applied terminal font '" << f.GetFaceName() << "' to "
              << static_cast<int>(SessionCount()) << " terminal(s)";
  SavePrefs();
}

void MainView::ApplyOptimizedDrawing() {
  bool optimized = AppManager::Get().GetPrefs().terminalOptimizedDrawing;
  for (auto *page : m_pages->GetAllSessions()) {
    for (auto *terminal : page->GetTerminals()) {
      terminal->EnableSafeDrawing(!optimized);
      terminal->Refresh();
    }
  }
}

void MainView::ApplyPrefs() {
  const auto &prefs = AppManager::Get().GetPrefs();
  ApplyTheme(prefs.terminalTheme);
  wxFont font;
  font.SetNativeFontInfo(prefs.terminalFontDesc);
  ApplyFont(font);
  ApplyOptimizedDrawing();

  // The theme of the application may have changed.
  ApplyToolBarStyles();

  for (auto *page : m_pages->GetAllSessions()) {
    for (auto *terminal : page->GetTerminals()) {
      terminal->SetBufferSize(prefs.scrollbackLines);
    }
  }
}

void MainView::ApplyToolBarStyles() {
  for (auto *page : m_pages->GetAllSessions()) {
    page->ApplyToolBarStyle();
  }
  for (auto *page : m_pages->GetFilePages()) {
    page->ApplyToolBarStyle();
  }
  m_fileExplorer->ApplyToolBarStyle();
  GetMainFrame()->ApplyToolBarStyle();
}

void MainView::ApplyTheme(const wxString &themeName) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetTheme(themeName);
  if (!active) {
    return;
  }
  for (auto *page : m_pages->GetAllSessions()) {
    page->ApplyTheme(*active);
    for (auto *terminal : page->GetTerminals()) {
      terminal->SendSizeEvent();
    }
  }
  for (auto *filePage : m_pages->GetFilePages()) {
    filePage->ApplyTheme(*active);
  }
  if (themeMgr.ActiveTheme()) {
    m_sessionsBook->SetBackgroundColour(themeMgr.ActiveTheme()->bg);
    m_sessionsBook->Refresh();
  }
  m_sessionsBook->SendSizeEvent();
  SavePrefs();

  // The application looks like the terminal theme: light or dark with it.
  if (AppManager::Get().GetPrefs().appTheme == kAppThemeFollowTerminal) {
    ApplyAppTheme(kAppThemeFollowTerminal);
    ApplyToolBarStyles();
  }
}

void MainView::SavePrefs() {
  auto &themeMgr = ThemeManager::Get();
  auto &prefs = AppManager::Get().GetPrefs();
  prefs.terminalTheme = themeMgr.CurrentThemeName();
  if (const auto theme = themeMgr.ActiveTheme(); theme && theme->font.IsOk()) {
    prefs.terminalFontDesc = theme->font.GetNativeFontInfoDesc();
  }

  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not persist UI prefs: " << st.message();
  }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
bool MainView::IsEmpty() const {
  return !m_flatView ||
         (m_flatView->GetItemCount() == 0 && m_filesView->GetItemCount() == 0 &&
          m_terminalsView->GetItemCount() == 0);
}

SessionGroup *MainView::GetSelectedGroup() const {
  // The group of the session that is showing (a file has none).
  auto *session = GetActiveSessionPage();
  return session ? m_pages->GetGroup(session->GetSession().groupName) : nullptr;
}

SessionPage *MainView::GetActiveSessionPage() const {
  return dynamic_cast<SessionPage *>(m_sessionsBook->GetCurrentPage());
}

std::optional<SessionRef> MainView::GetActivePageRef() const {
  return RefOf(m_sessionsBook->GetCurrentPage());
}

wxArrayString MainView::GetGroupNames() const {
  return m_pages->GetSessionGroupNames();
}

bool MainView::IsNameExist(const wxString &name,
                           const wxString &groupName) const {
  return m_pages->FindSession(groupName, name) != nullptr;
}

size_t MainView::GroupCount() const { return m_pages->GroupCount(); }

size_t MainView::SessionCount() const {
  return m_pages->GetAllSessions().size();
}

size_t MainView::PageCount() const { return m_pages->GetPages().size(); }

bool MainView::CanRefreshCurrent() const {
  auto *group = GetSelectedGroup();
  return group && group->IsSessionGroup() && GetActiveSessionPage() != nullptr;
}

void MainView::ShowFind() {
  wxWindow *page = m_sessionsBook->GetCurrentPage();
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    file->ShowFind();
  } else if (auto *session = dynamic_cast<SessionPage *>(page)) {
    session->ShowFind();
  }
}

void MainView::FindNext(bool forward) {
  wxWindow *page = m_sessionsBook->GetCurrentPage();
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    file->FindNext(forward);
  } else if (auto *session = dynamic_cast<SessionPage *>(page)) {
    session->FindNext(forward);
  }
}

bool MainView::CanFind() const {
  wxWindow *page = m_sessionsBook->GetCurrentPage();
  return dynamic_cast<FilePage *>(page) != nullptr ||
         dynamic_cast<SessionPage *>(page) != nullptr;
}

bool MainView::CanFindNext() const {
  wxWindow *page = m_sessionsBook->GetCurrentPage();
  if (dynamic_cast<FilePage *>(page) != nullptr) {
    return true;
  }
  auto *session = dynamic_cast<SessionPage *>(page);
  return session != nullptr && session->HasSearch();
}

void MainView::GotoLine() {
  auto *file = dynamic_cast<FilePage *>(m_sessionsBook->GetCurrentPage());
  if (file == nullptr) {
    return;
  }
  if (!m_gotoBar->IsShown()) {
    // Not when it is open already (Ctrl+G in the box): the caret has followed
    // the text by now, and Escape must go back to where the user was.
    m_gotoFileKey = file->GetKey();
    m_gotoView = file->GetViewState();
    m_gotoBar->Show();
    m_gotoBar->GetParent()->GetSizer()->Layout();
  }
  m_gotoBar->SetInitialText(":");
}

bool MainView::CanGotoLine() const {
  return dynamic_cast<FilePage *>(m_sessionsBook->GetCurrentPage()) != nullptr;
}

void MainView::OnGotoChanged(wxCommandEvent &event) {
  // ":12" or ":12:5": the caret follows as the user types.
  const wxString text = event.GetString();
  auto *file = dynamic_cast<FilePage *>(m_sessionsBook->GetCurrentPage());
  int line = 0;
  int column = 0;
  if (file != nullptr && text.StartsWith(":") &&
      ParseLineColumn(text.Mid(1), &line, &column)) {
    file->GotoLine(line, std::max(column, 1));
  }
}

void MainView::OnGotoClosed(wxCommandEvent &event) {
  // The bar has hidden itself already: take its place back, whatever the page.
  m_gotoBar->GetParent()->GetSizer()->Layout();
  auto *file = dynamic_cast<FilePage *>(m_sessionsBook->GetCurrentPage());
  if (file == nullptr) {
    return;
  }
  switch (event.GetInt()) {
  case GotoAnythingBar::kCancelled:
    if (file->GetKey() == m_gotoFileKey) {
      file->SetViewState(m_gotoView); // As it was before the box opened
    }
    file->FocusEditor();
    break;
  case GotoAnythingBar::kAccepted:
    OnGotoChanged(event); // The last text, if it was not shown yet
    file->FocusEditor();
    break;
  default:
    break; // The user clicked somewhere: leave the caret and the focus
  }
}

// ---------------------------------------------------------------------------
// Session / group actions
// ---------------------------------------------------------------------------

void MainView::RefreshCurrentSelection() {
  auto *group = GetSelectedGroup();
  CHECK_NOT_NULL_RETURN(group);
  auto *page = GetActiveSessionPage();
  if (group->IsSessionGroup() && page != nullptr) {
    page->Restart();
  }
}

void MainView::RefreshGroup(SessionGroup *group) {
  if (group == nullptr || !group->IsSessionGroup()) {
    return;
  }
  for (auto *page : m_pages->GetGroupSessions(group->GetGroupName())) {
    page->CallAfter(&SessionPage::Restart);
    m_pendingIdle++;
  }
  if (m_pendingIdle > 0) {
    GetMainFrame()->SetActivityText(
        wxString::Format(_("Refreshing %d sessions"), m_pendingIdle));
    GetMainFrame()->StartActivityIndicator();
  }
}

void MainView::CloseAllSessions() {
  if (SessionCount() == 0) {
    return;
  }
  wxString msg;
  msg << _("This operation will close ALL sessions.\nContinue?");
  if (wxMessageBox(msg, "Kennel",
                   wxICON_WARNING | wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT) !=
      wxYES) {
    return;
  }
  CallAfter(&MainView::DeleteAll);
}

void MainView::DeleteAll() {
  m_closeAfterSave.clear();
  for (const SessionFile &file : m_restoringFiles) {
    m_ignoreRemote.insert(KeyOfFile(file));
  }
  m_restoringFiles.clear();
  m_selectWhenRestored.clear();
  m_sessionsBook->DeleteAllPages();
  m_pages->Clear();
  m_flatView->Clear();
  m_filesView->Clear();
  m_terminalsView->Clear();
  m_fileExplorer->Clear();
  m_recent.clear();
  GetMainFrame()->ClearSessionStatus();
  // A page destroyed mid-save never delivers wxEVT_FILE_SAVE_DONE.
  GetMainFrame()->StopActivityIndicator();
  GetMainFrame()->ClearActivityText();
  SyncWorkspaceToDisk();
}

void MainView::DeleteGroupByName(const wxString &name) {
  auto *group = m_pages->GetGroup(name);
  CHECK_NOT_NULL_RETURN(group);

  auto sessions = m_pages->GetGroupSessions(name);
  if (!sessions.empty()) {
    wxString msg;
    msg << _("This will close ") << sessions.size()
        << _(" session(s).\nContinue?");
    if (wxMessageBox(msg, "Kennel",
                     wxICON_WARNING | wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT) !=
        wxYES) {
      return;
    }
  }

  // The status bar is cleared only when the page that is showing goes away.
  const bool activeDeleted =
      std::find(sessions.begin(), sessions.end(),
                m_sessionsBook->GetCurrentPage()) != sessions.end();
  for (auto *page : sessions) {
    int where = m_sessionsBook->FindPage(page);
    if (where != wxNOT_FOUND) {
      m_sessionsBook->DeletePage(where);
    }
  }
  m_pages->RemoveGroup(name); // deletes the group with its container
  RefreshViews();
  if (activeDeleted) {
    GetMainFrame()->ClearSessionStatus();
  }

  SyncWorkspaceToDisk();

  if (m_pages->GroupCount() > 0) {
    ActivateGroup(m_pages->GetFirstGroupName());
  } else {
    wxTheApp->GetTopWindow()->SetLabel(_("Kennel"));
  }
}

void MainView::RenameGroup(SessionGroup *group) {
  CHECK_NOT_NULL_RETURN(group);
  if (group->IsDefaultGroup()) {
    return;
  }

  wxString oldName = group->GetGroupName();
  wxString newName =
      ::wxGetTextFromUser(_("Choose new group name:"), "Kennel", oldName, this);
  if (newName.empty() || newName == oldName) {
    return;
  }

  if (IsReservedGroupName(newName)) {
    wxMessageBox(wxString::Format(_("'%s' is a reserved name"), newName),
                 "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }

  if (m_pages->GetGroup(newName) != nullptr) {
    wxMessageBox(
        wxString::Format(_("A group named '%s' already exists"), newName),
        "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }

  for (auto *page : m_pages->GetGroupSessions(oldName)) {
    RenameRecent(SessionRef{oldName, page->GetSession().name},
                 SessionRef{newName, page->GetSession().name});
  }
  m_pages->RenameGroup(group, newName);
  RefreshViews();
  SyncWorkspaceToDisk();
}

void MainView::DuplicateSession(SessionPage *page) {
  CHECK_NOT_NULL_RETURN(page);
  const auto &session = page->GetSession();

  wxString candidate;
  int suffix = 1;
  do {
    candidate = wxString::Format("%s%d", session.name, suffix++);
  } while (IsNameExist(candidate, session.groupName));

  StartAgentDialog dlg(this);
  dlg.SetSelectedClientName(session.agentName);
  dlg.SetSelectedGroup(session.groupName);
  dlg.SetSessionName(candidate);

  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  LaunchSession(dlg.GetRequest());
}

void MainView::RenameSession(SessionPage *page) {
  CHECK_NOT_NULL_RETURN(page);
  const wxString oldName = page->GetSession().name;
  wxString newName =
      ::wxGetTextFromUser(_("New name:"), "Kennel", oldName, this);
  if (newName.empty() || newName == oldName) {
    return;
  }

  if (IsNameExist(newName, page->GetSession().groupName)) {
    wxMessageBox(_("A session with this name already exists in this group"),
                 "Kennel", wxICON_WARNING | wxOK | wxCENTER, this);
    return;
  }

  RenameRecent(SessionRef{page->GetSession().groupName, oldName},
               SessionRef{page->GetSession().groupName, newName});
  page->GetSession().name = newName;
  page->SetDefaultSessionName(newName);
  RefreshViews();
  SyncWorkspaceToDisk();
}

void MainView::RenameItem() {
  if (auto *session = GetActiveSessionPage()) {
    RenameSession(session);
  }
}

void MainView::CloseSessionByName(const wxString &sessionName) {
  for (auto *group : m_pages->GetGroups()) {
    if (m_pages->FindSession(group->GetGroupName(), sessionName)) {
      CloseSession(group->GetGroupName(), sessionName);
      return;
    }
  }
}

void MainView::CloseSession(const wxString &groupName,
                            const wxString &sessionName) {
  auto *page = m_pages->FindSession(groupName, sessionName);
  CHECK_NOT_NULL_RETURN(page);
  RemovePage(page);
  SyncWorkspaceToDisk();
}

void MainView::MoveSessionToGroup(const wxString &sessionName,
                                  const wxString &fromGroupName,
                                  const wxString &toGroupName) {
  KLOG_DEBUG() << "Moving session: " << sessionName
               << " from: " << fromGroupName << "->" << toGroupName;

  auto *page = m_pages->FindSession(fromGroupName, sessionName);
  CHECK_NOT_NULL_RETURN(page);

  if (IsNameExist(sessionName, toGroupName)) {
    wxMessageBox(
        wxString::Format(_("Cannot move session '%s' to group '%s': a session "
                           "with that name already exists there."),
                         sessionName, toGroupName),
        "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }

  const bool wasActive = (GetActiveSessionPage() == page);
  RenameRecent(SessionRef{fromGroupName, sessionName},
               SessionRef{toGroupName, sessionName});
  m_pages->MoveSession(page, toGroupName);
  // MoveSession re-created the leaf without its status: put it back.
  ApplyPageIcon(page);
  // The old group may be empty now. Deferred, see RemovePage().
  CallAfter(&MainView::RemoveEmptyGroups);

  RefreshViews();
  SyncWorkspaceToDisk();

  if (wasActive) {
    SelectSessionPage(page);
  }
}

void MainView::OnSessionIdle(wxCommandEvent &e) {
  e.Skip();
  if (m_pendingIdle > 0) {
    m_pendingIdle--;
  }

  if (m_pendingIdle == 0) {
    GetMainFrame()->StopActivityIndicator();
    GetMainFrame()->ClearActivityText();
  } else if (m_pendingIdle > 0) {
    GetMainFrame()->SetActivityText(
        wxString::Format(_("Refreshing %d sessions"), m_pendingIdle));
  }
}

void MainView::OnSessionActive(wxCommandEvent &e) { e.Skip(); }

void MainView::OnSessionCwd(wxCommandEvent &e) {
  e.Skip();
  RefreshViews();
  // A restored terminal starts in the folder it was left in. Agents are
  // restored in their launch folder: nothing to save for them.
  // The event is pending: the page may be gone. Compare the pointer only.
  for (auto *page : m_pages->GetAllSessions()) {
    if (page == e.GetEventObject()) {
      if (page->GetSession().plainTerminal) {
        SyncWorkspaceSoon();
      }
      break;
    }
  }
}

void MainView::OnSessionProgram(wxCommandEvent &e) {
  e.Skip();
  // The event is pending: the page may be gone. Compare the pointer only.
  for (auto *page : m_pages->GetAllSessions()) {
    if (page == e.GetEventObject()) {
      // Nobody needs a check mark on the page that is showing.
      if (page == GetActiveSessionPage()) {
        page->AcknowledgeDone();
      }
      ApplyPageIcon(page);
      break;
    }
  }
  UpdateSpinnerTimer();
}

void MainView::OnSpinnerTimer(wxTimerEvent &) {
  m_spinnerFrame = (m_spinnerFrame + 1) % kSpinnerFrameCount;
  bool working = false;
  for (auto *page : m_pages->GetAllSessions()) {
    if (page->GetProgramActivity() == SessionPage::ProgramActivity::Working) {
      working = true;
      ApplyPageIcon(page);
    }
  }
  if (!working) {
    m_spinnerTimer.Stop();
  }
}

void MainView::UpdateSpinnerTimer() {
  if (m_spinnerTimer.IsRunning()) {
    return; // It stops itself when nobody is working any more.
  }
  for (auto *page : m_pages->GetAllSessions()) {
    if (page->GetProgramActivity() == SessionPage::ProgramActivity::Working) {
      constexpr int kSpinnerMs = 100;
      m_spinnerTimer.Start(kSpinnerMs);
      return;
    }
  }
}

void MainView::ApplyPageIcon(SessionPage *page) {
  // Not ok when the program does nothing.
  wxBitmapBundle status;
  switch (page->GetProgramActivity()) {
  case SessionPage::ProgramActivity::Working:
    status = m_spinnerFrames[m_spinnerFrame];
    break;
  case SessionPage::ProgramActivity::Done:
    status = m_checkIcon;
    break;
  case SessionPage::ProgramActivity::Blocked:
    // The program needs the user: it raises its hand.
    status = m_handIcon;
    break;
  case SessionPage::ProgramActivity::None:
    break;
  }
  m_pages->SetPageStatusIcon(page, status);
  const Session &session = page->GetSession();
  // The view that shows the page shows the status; the others ignore it.
  const SessionRef ref{session.groupName, session.name};
  const wxBitmapBundle baseIcon = SessionIconFor(session);
  m_flatView->SetPageStatusIcon(ref, status, baseIcon);
  m_terminalsView->SetPageStatusIcon(ref, status, baseIcon);
}

void MainView::OnSessionExited(wxCommandEvent &e) {
  const wxString &sessionName = e.GetString();
  for (auto *group : m_pages->GetGroups()) {
    if (auto *page = m_pages->FindSession(group->GetGroupName(), sessionName)) {
      const Session &session = page->GetSession();
      if (session.IsJobRun()) {
        AppendJobLogEntry(JobLogEntry{
            .event = "end",
            .job = session.jobName,
            .session = sessionName,
            .message =
                wxString::Format("Job '%s' terminal closed", session.jobName),
        });
      }
      break;
    }
  }

  // Deferred: removing the session (and its now-empty group, if any)
  // synchronously from inside this handler would destroy the page that is
  // sending the event.
  CallAfter(&MainView::CloseSessionByName, e.GetString());
}

void MainView::OnIdleEvent(wxIdleEvent &e) {
  if (!m_idleHandled && GetActiveSessionPage()) {
    m_idleHandled = true;
    GetActiveSessionPage()->SetFocus();
  }
}

void MainView::LoadBitmaps() {
  auto &bmps = AppManager::Get().GetBitmaps();
  bmps.Load("home.svg");
  bmps.AddAlias("home.svg", "home");

  bmps.Load("folder.svg");
  bmps.AddAlias("folder.svg", "folder");

  bmps.Load("folder-open.svg");
  bmps.AddAlias("folder-open.svg", "folder-open");

  bmps.Load("group-default.svg");
  bmps.AddAlias("group-default.svg", "group-default");

  for (const char *alias : kGroupIconAliases) {
    wxString filename = wxString(alias) + ".svg";
    bmps.Load(filename);
    bmps.AddAlias(filename, alias);
  }

  bmps.Load("up.svg");
  bmps.AddAlias("up.svg", "up");

  bmps.Load("link.svg");
  bmps.AddAlias("link.svg", "link");

  bmps.Load("terminal.svg");
  bmps.AddAlias("terminal.svg", "terminal");

  bmps.Load("terminals.svg");
  bmps.AddAlias("terminals.svg", "terminals");

  bmps.Load("list-view.svg");
  bmps.AddAlias("list-view.svg", "list-view");

  bmps.Load("restart.svg");
  bmps.AddAlias("restart.svg", "restart");

  bmps.Load("new.svg");
  bmps.AddAlias("new.svg", "new");

  bmps.Load("agent.svg");
  bmps.AddAlias("agent.svg", "agent");

  bmps.Load("job.svg");
  bmps.AddAlias("job.svg", "job");

  bmps.Load("pin.svg");
  bmps.AddAlias("pin.svg", "pin");

  bmps.Load("save.svg");
  bmps.AddAlias("save.svg", "save");

  bmps.Load("reload.svg");
  bmps.AddAlias("reload.svg", "reload");

  bmps.Load("close.svg");
  bmps.AddAlias("close.svg", "close");

  bmps.Load("copy.svg");
  bmps.AddAlias("copy.svg", "copy");

  bmps.Load("paste.svg");
  bmps.AddAlias("paste.svg", "paste");

  bmps.Load("find.svg");
  bmps.AddAlias("find.svg", "find");

  bmps.Load("code-reviewer.svg");
  bmps.AddAlias("code-reviewer.svg", "code-reviewer");

  bmps.LoadMimeIcons();

  // Load file*.svg
  wxArrayString files;
  wxDir::GetAllFiles(ShippedAssetsDir().GetPath(), &files, "file*.svg",
                     wxDIR_FILES);
  for (const wxString &file : files) {
    wxFileName fn{file};
    bmps.Load(fn.GetFullName());
    bmps.AddAlias(fn.GetFullName(), fn.GetName());
  }

  const auto &agents = AppManager::Get().Adapters().Agents();
  for (const auto &agent : agents) {
    if (wxFileExists(agent.iconPath)) {
      bmps.Load(agent.iconPath);
      // Alias by agent name so a session leaf can be restored to its
      // agent's icon (see SetAgentIcon).
      bmps.AddAlias(agent.iconPath, agent.name);
    }
  }
}
