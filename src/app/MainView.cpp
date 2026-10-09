#include "MainView.hpp"

#include "MainFrame.h"
#include "StartAgentDialog.hpp"
#include "ThemeLoader.h"
#include "ThemeManager.h"
#include "app/AssetBootstrap.h"
#include "app/FilePage.hpp"
#include "app/FlatView.hpp"
#include "app/PageSwitcherDlg.hpp"
#include "app/SessionGroup.h"
#include "app/SessionPage.hpp"
#include "app/TreeView.hpp"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/EventNotifier.hpp"
#include "core/JobLog.h"
#include "core/Logger.h"
#include "core/SftpClient.h"
#include "core/Workspace.h"
#include "core/WorkspaceStore.h"

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
// Moves `splitter`'s sash to `dip` device-independent pixels, if there is a
// saved one.
void ApplySashPosition(wxSplitterWindow *splitter, int dip) {
  if (dip > 0 && splitter->IsSplit()) {
    splitter->SetSashPosition(splitter->FromDIP(dip));
  }
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

  // The views need the bitmaps (group icons) loaded first.
  m_treeView = new TreeView(GetSplitterPageLeftTop());
  FillPanel(GetSplitterPageLeftTop(), m_treeView);
  m_flatView = new FlatView(GetSplitterPageLeftBottom());
  FillPanel(GetSplitterPageLeftBottom(), m_flatView);

  for (int i = 0; i < kSpinnerFrameCount; ++i) {
    wxString name;
    name.Printf("spinner-%d.svg", i);
    const wxString path = ResolveIconPath(name);
    if (!path.empty() && wxFileName::FileExists(path)) {
      m_spinnerFrames[i] = wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
    }
  }

  EventNotifier::Get()->Bind(wxEVT_PAGEVIEW_SELECTED, &MainView::OnPageSelected,
                             this);
  Bind(wxEVT_PAGEVIEW_MENU, &MainView::OnPageMenu, this);
  Bind(wxEVT_SESSION_IDLE, &MainView::OnSessionIdle, this);
  Bind(wxEVT_SESSION_ACTIVE, &MainView::OnSessionActive, this);
  Bind(wxEVT_SESSION_EXITED, &MainView::OnSessionExited, this);
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
  auto *page = m_treeView->FindPage(event.GetRef());
  if (page == nullptr || page == m_sessionsBook->GetCurrentPage()) {
    return;
  }
  // The sender has selected it already, and the other views follow the same
  // event: no need to tell them.
  ShowPage(page, event.UpdateRecent(), false);
}

void MainView::OnPageMenu(PageViewEvent &event) {
  auto *on = dynamic_cast<wxWindow *>(event.GetEventObject());
  CHECK_NOT_NULL_RETURN(on);
  if (!event.GetSessionName().empty()) {
    if (auto *page = m_treeView->FindPage(event.GetRef())) {
      ShowPageMenu(page, on);
    }
  } else if (!event.GetGroupName().empty()) {
    ShowGroupMenu(event.GetGroupName(), on);
  } else {
    ShowBackgroundMenu(on);
  }
}

void MainView::RefreshFlatView() {
  if (m_flatRefreshPending) {
    return;
  }
  m_flatRefreshPending = true;
  CallAfter(&MainView::DoRefreshFlatView);
}

void MainView::DoRefreshFlatView() {
  m_flatRefreshPending = false;
  // Tree order; the list sorts it if the user picked a column.
  m_flatView->SetPages(m_treeView->GetPages(), GetActivePageRef());
}

std::vector<PageInfo>
MainView::GetPagesByRecency(const std::optional<SessionRef> &first) const {
  std::vector<PageInfo> remaining = m_treeView->GetPages();
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
    return r == ref || m_treeView->FindPage(r) == nullptr;
  });
  m_recent.insert(m_recent.begin(), ref);
  SyncWorkspaceSoon();
}

void MainView::RenameRecent(const SessionRef &from, const SessionRef &to) {
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
  for (auto *group : m_treeView->GetGroups()) {
    if (!group->IsTerminalsGroup() && !group->IsDefaultGroup() &&
        !group->GetIcon().empty()) {
      ws.groups.push_back(GroupMeta{group->GetGroupName(), group->GetIcon()});
    }
    for (auto *page : m_treeView->GetGroupSessions(group->GetGroupName())) {
      ws.sessions.push_back(page->GetSession());
    }
  }

  for (const auto &info : GetPagesByRecency(std::nullopt)) {
    auto *session =
        dynamic_cast<SessionPage *>(m_treeView->FindPage(info.Ref()));
    if (session && session->GetSession().IsPersistent()) {
      ws.recentSessions.push_back(SessionRef{session->GetSession().groupName,
                                             session->GetSession().name});
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
    if (auto *group = m_treeView->GetGroup(session->GetSession().groupName)) {
      group->SetLastActive(session->GetSession().name);
    }
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
}

void MainView::SelectFilePage(FilePage *page, bool updateRecent,
                              bool notifyViews) {
  CHECK_NOT_NULL_RETURN(page);
  ActivatePage(page, updateRecent, notifyViews);
  page->CallAfter(&FilePage::FocusEditor);
  wxTheApp->GetTopWindow()->SetLabel(page->GetPath());
}

void MainView::ActivateGroup(const wxString &groupName) {
  if (!m_treeView->SelectGroup(groupName)) {
    return;
  }
  ShowPage(m_treeView->GetGroupDefaultPage(groupName));
}

void MainView::SelectFallbackPage(const wxString &preferredGroup) {
  ShowPage(m_treeView->GetFallbackPage(preferredGroup));
}

void MainView::SwitchPage(bool forward) {
  const auto ordered = GetPagesByRecency(GetActivePageRef());
  if (ordered.size() <= 1) {
    return;
  }

  const int count = static_cast<int>(ordered.size());
  int chosen = forward ? 1 : count - 1;
  // With Ctrl already released (e.g. picked from the menu) there is nothing to
  // hold on to, so skip the popup.
  if (wxGetMouseState().RawControlDown()) {
    std::vector<PageSwitcherItem> items;
    for (const auto &info : ordered) {
      items.push_back({info.label + "  -  " + info.group, info.icon});
    }
    PageSwitcherDlg dlg(wxGetTopLevelParent(this), items, forward);
    if (dlg.ShowModal() != wxID_OK) {
      return;
    }
    chosen = dlg.GetSelectedIndex();
  }
  if (chosen < 0 || chosen >= count) {
    return;
  }
  ShowPage(m_treeView->FindPage(ordered[chosen].Ref()));
}

// ---------------------------------------------------------------------------
// Adding / removing pages
// ---------------------------------------------------------------------------

SessionPage *MainView::AddSession(SessionPage *page) {
  if (!m_treeView->AddSession(page)) {
    return nullptr;
  }
  m_sessionsBook->AddPage(page, page->GetSession().name, false);
  RefreshFlatView();
  return page;
}

SessionPage *MainView::AddSessionPage(const Session &session, bool resume) {
  auto *group = m_treeView->EnsureGroup(session.groupName);
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
  m_treeView->AddFile(page);
  RefreshFlatView();
}

void MainView::RemovePage(wxWindow *page) {
  const bool wasActive = (m_sessionsBook->GetCurrentPage() == page);

  // Prefer a sibling of a closed session. Sessions are looked at by group
  // name since the group itself may disappear with its last session.
  wxString preferredGroup;
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    const wxString &groupName = session->GetSession().groupName;
    if (m_treeView->GetGroupSessions(groupName).size() > 1) {
      preferredGroup = groupName;
    }
  }

  m_treeView->RemovePage(page);
  int where = m_sessionsBook->FindPage(page);
  if (where != wxNOT_FOUND) {
    m_sessionsBook->DeletePage(where); // destroys the page window
  }
  RefreshFlatView();

  // Deferred: selecting a fallback page touches the tree control right after
  // DeleteItem() above, which can crash the native macOS outline view
  // mid-redraw (see OnSessionExited for the same issue).
  if (wasActive) {
    CallAfter(&MainView::SelectFallbackPage, preferredGroup);
  }
  // Cleanup the tree from empty groups
  CallAfter(&MainView::RemoveEmptyGroups);
}

void MainView::RemoveEmptyGroups() { m_treeView->RemoveEmptyGroups(); }

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

void MainView::OnFileSaveStarted(FileEvent &e) {
  auto *page = m_treeView->FindFile(e.GetKey());
  const wxString name = page ? page->GetDisplayName() : e.GetFilePath();
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
                              const std::vector<wxString> &searchDirs) {
  KLOG_DEBUG() << "Opening remote file: " << clickedPath;
  if (!m_fetchingRemote.insert(FileEvent::MakeKey(clickedPath, remoteHost))
           .second) {
    return; // A previous click is still connecting/downloading.
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

void MainView::OnRemoteFileRead(const RemoteReadResult &result) {
  m_fetchingRemote.erase(
      FileEvent::MakeKey(result.clickedPath, result.remoteHost));

  if (!result.ok) {
    KLOG_WARN() << "Remote open failed for '" << result.clickedPath
                << "': " << result.error;
    ::wxMessageBox(result.error, "Kennel", wxICON_WARNING | wxOK, this);
    return;
  }

  const wxString &path = result.path;
  if (result.content.find('\0') != std::string::npos) {
    ::wxMessageBox(wxString::Format(_("%s is a binary file."), path), "Kennel",
                   wxICON_INFORMATION | wxOK, this);
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
  ShowRemoteFile(result.remoteHost, path, text, editable);
}

void MainView::OpenLocalFile(const wxString &path) {
  const wxString key = wxFileName(path).GetFullPath();
  if (auto *existing = m_treeView->FindFile(key)) {
    SelectFilePage(existing);
    return;
  }

  auto *page = new FilePage(
      m_sessionsBook, key,
      ThemeManager::Get().ActiveTheme().value_or(wxTerminalTheme{}));
  if (!page->LoadLocal()) {
    page->Destroy();
    wxMessageBox(wxString::Format(_("Could not open %s"), key), "Kennel",
                 wxOK | wxICON_ERROR, this);
    return;
  }
  AddFilePage(page);
  SelectFilePage(page);
}

void MainView::ShowRemoteFile(const RemoteHostDetails &remoteHost,
                              const wxString &path, const wxString &text,
                              bool editable) {
  const wxString key = FileEvent::MakeKey(path, remoteHost);
  if (auto *existing = m_treeView->FindFile(key)) {
    // The remote copy may have changed, but never clobber unsaved edits.
    if (!existing->IsModified()) {
      existing->LoadRemote(text, editable);
    }
    SelectFilePage(existing);
    return;
  }

  auto *page = new FilePage(
      m_sessionsBook, path,
      ThemeManager::Get().ActiveTheme().value_or(wxTerminalTheme{}),
      remoteHost);
  page->LoadRemote(text, editable);
  AddFilePage(page);
  SelectFilePage(page);
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
}

void MainView::CloseFileByKey(const wxString &key) {
  if (auto *page = m_treeView->FindFile(key)) {
    CloseFile(page);
  }
}

void MainView::CloseAllFiles() {
  std::vector<wxString> keys;
  for (auto *page : m_treeView->GetFilePages()) {
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
  static int terminalId{0};
  const auto &prefs = AppManager::Get().GetPrefs();
  NewSessionRequest request{
      .name = wxString::Format(_("Terminal %d"), ++terminalId),
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

void MainView::RestoreLayout() {
  // Deferred: the sash can only be moved to where the window is big enough
  // for it, and the window is not at its final size (e.g. maximized) before
  // the first pass of the event loop.
  CallAfter([this] {
    const auto &prefs = AppManager::Get().GetPrefs();
    ApplySashPosition(GetSplitterMain(), prefs.sidebarWidth);
    ApplySashPosition(GetSplitterLeftVertical(), prefs.treePaneHeight);
    m_layoutRestored = true;
  });
}

void MainView::SaveLayout() {
  // Before the saved layout was applied the splitters still hold their
  // defaults, which must not replace what was saved.
  if (!m_layoutRestored) {
    return;
  }
  auto &prefs = AppManager::Get().GetPrefs();
  if (GetSplitterMain()->IsSplit()) {
    prefs.sidebarWidth =
        GetSplitterMain()->ToDIP(GetSplitterMain()->GetSashPosition());
  }
  if (GetSplitterLeftVertical()->IsSplit()) {
    prefs.treePaneHeight = GetSplitterLeftVertical()->ToDIP(
        GetSplitterLeftVertical()->GetSashPosition());
  }
}

void MainView::RestoreSessions() {
  const auto &initial = AppManager::Get().InitialWorkspace();
  if (initial.sessions.empty()) {
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

  // Bring back the recent order and show the page used last.
  m_recent.clear();
  SessionPage *lastUsed = nullptr;
  for (const SessionRef &r : initial.recentSessions) {
    if (auto *page = m_treeView->FindSession(r.groupName, r.name)) {
      m_recent.push_back(r);
      if (lastUsed == nullptr) {
        lastUsed = page;
      }
    }
  }
  if (lastUsed != nullptr) {
    SelectSessionPage(lastUsed);
  } else if (m_treeView->GroupCount() > 0) {
    ActivateGroup(m_treeView->GetFirstGroupName());
  }

  // Anything that failed to restore into the UI (e.g. its agent no longer
  // exists) must not be written back — the UI is the only source of truth.
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
  for (auto *page : m_treeView->GetAllSessions()) {
    page->ApplyTheme(*active);
    for (auto *terminal : page->GetTerminals()) {
      terminal->SendSizeEvent();
    }
  }
  for (auto *filePage : m_treeView->GetFilePages()) {
    filePage->ApplyTheme(*active);
  }
  m_sessionsBook->SendSizeEvent();
  KLOG_INFO() << "Applied terminal font '" << f.GetFaceName() << "' to "
              << static_cast<int>(SessionCount()) << " terminal(s)";
  SavePrefs();
}

void MainView::ApplyOptimizedDrawing() {
  bool optimized = AppManager::Get().GetPrefs().terminalOptimizedDrawing;
  for (auto *page : m_treeView->GetAllSessions()) {
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

  for (auto *page : m_treeView->GetAllSessions()) {
    for (auto *terminal : page->GetTerminals()) {
      terminal->SetBufferSize(prefs.scrollbackLines);
    }
  }
}

void MainView::ApplyTheme(const wxString &themeName) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetTheme(themeName);
  if (!active) {
    return;
  }
  for (auto *page : m_treeView->GetAllSessions()) {
    page->ApplyTheme(*active);
    for (auto *terminal : page->GetTerminals()) {
      terminal->SendSizeEvent();
    }
  }
  for (auto *filePage : m_treeView->GetFilePages()) {
    filePage->ApplyTheme(*active);
  }
  if (themeMgr.ActiveTheme()) {
    m_sessionsBook->SetBackgroundColour(themeMgr.ActiveTheme()->bg);
    m_sessionsBook->Refresh();
  }
  m_sessionsBook->SendSizeEvent();
  SavePrefs();
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
  return !m_flatView || m_flatView->GetItemCount() == 0;
}

SessionGroup *MainView::GetSelectedGroup() const {
  return m_treeView->GetSelectedGroup();
}

SessionPage *MainView::GetActiveSessionPage() const {
  return dynamic_cast<SessionPage *>(m_sessionsBook->GetCurrentPage());
}

std::optional<SessionRef> MainView::GetActivePageRef() const {
  return RefOf(m_sessionsBook->GetCurrentPage());
}

wxArrayString MainView::GetGroupNames() const {
  return m_treeView->GetSessionGroupNames();
}

bool MainView::IsNameExist(const wxString &name,
                           const wxString &groupName) const {
  return m_treeView->FindSession(groupName, name) != nullptr;
}

size_t MainView::GroupCount() const { return m_treeView->GroupCount(); }

size_t MainView::SessionCount() const {
  return m_treeView->GetAllSessions().size();
}

size_t MainView::PageCount() const { return m_treeView->GetPages().size(); }

bool MainView::CanRefreshCurrent() const {
  auto *group = GetSelectedGroup();
  return group && group->IsSessionGroup() && GetActiveSessionPage() != nullptr;
}

bool MainView::IsSelectionSessionGroup() const {
  auto *group = GetSelectedGroup();
  return group && group->IsSessionGroup();
}

bool MainView::IsSelectionTerminalGroup() const {
  auto *group = GetSelectedGroup();
  return group && group->IsTerminalsGroup();
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

void MainView::RefreshSelectedGroup() { RefreshGroup(GetSelectedGroup()); }

void MainView::RefreshGroup(SessionGroup *group) {
  if (group == nullptr || !group->IsSessionGroup()) {
    return;
  }
  for (auto *page : m_treeView->GetGroupSessions(group->GetGroupName())) {
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
  m_sessionsBook->DeleteAllPages();
  m_treeView->Clear();
  m_flatView->Clear();
  m_recent.clear();
  // A page destroyed mid-save never delivers wxEVT_FILE_SAVE_DONE.
  GetMainFrame()->StopActivityIndicator();
  GetMainFrame()->ClearActivityText();
  SyncWorkspaceToDisk();
}

void MainView::DeleteGroupByName(const wxString &name) {
  auto *group = m_treeView->GetGroup(name);
  CHECK_NOT_NULL_RETURN(group);

  auto sessions = m_treeView->GetGroupSessions(name);
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

  for (auto *page : sessions) {
    int where = m_sessionsBook->FindPage(page);
    if (where != wxNOT_FOUND) {
      m_sessionsBook->DeletePage(where);
    }
  }
  m_treeView->RemoveGroup(name); // deletes the group with its container
  RefreshFlatView();

  SyncWorkspaceToDisk();

  if (m_treeView->GroupCount() > 0) {
    ActivateGroup(m_treeView->GetFirstGroupName());
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

  if (m_treeView->GetGroup(newName) != nullptr) {
    wxMessageBox(
        wxString::Format(_("A group named '%s' already exists"), newName),
        "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }

  for (auto *page : m_treeView->GetGroupSessions(oldName)) {
    RenameRecent(SessionRef{oldName, page->GetSession().name},
                 SessionRef{newName, page->GetSession().name});
  }
  m_treeView->RenameGroup(group, newName);
  RefreshFlatView();
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
  m_treeView->UpdateLabel(page);
  RefreshFlatView();
  SyncWorkspaceToDisk();
}

void MainView::RenameItem() {
  if (auto *page = m_treeView->GetSelectedPage()) {
    if (auto *session = dynamic_cast<SessionPage *>(page)) {
      RenameSession(session);
    }
    return;
  }
  if (auto *group = m_treeView->GetSelectedGroupNode()) {
    RenameGroup(group);
  }
}

void MainView::CloseSessionByName(const wxString &sessionName) {
  for (auto *group : m_treeView->GetGroups()) {
    if (m_treeView->FindSession(group->GetGroupName(), sessionName)) {
      CloseSession(group->GetGroupName(), sessionName);
      return;
    }
  }
}

void MainView::CloseSession(const wxString &groupName,
                            const wxString &sessionName) {
  auto *page = m_treeView->FindSession(groupName, sessionName);
  CHECK_NOT_NULL_RETURN(page);
  RemovePage(page);
  SyncWorkspaceToDisk();
}

void MainView::MoveSessionToGroup(const wxString &sessionName,
                                  const wxString &fromGroupName,
                                  const wxString &toGroupName) {
  KLOG_DEBUG() << "Moving session: " << sessionName
               << " from: " << fromGroupName << "->" << toGroupName;

  auto *page = m_treeView->FindSession(fromGroupName, sessionName);
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
  m_treeView->MoveSession(page, toGroupName);
  // The old group may be empty now. Deferred, see RemovePage().
  CallAfter(&MainView::RemoveEmptyGroups);

  RefreshFlatView();
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

void MainView::OnSessionExited(wxCommandEvent &e) {
  const wxString &sessionName = e.GetString();
  for (auto *group : m_treeView->GetGroups()) {
    if (auto *page =
            m_treeView->FindSession(group->GetGroupName(), sessionName)) {
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

  // Deferred: deleting the session's tree leaf (and its now-empty group
  // container, if any) synchronously from inside this handler can crash the
  // native macOS outline view mid-redraw.
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

  bmps.Load("terminal.svg");
  bmps.AddAlias("terminal.svg", "terminal");

  bmps.Load("terminals.svg");
  bmps.AddAlias("terminals.svg", "terminals");

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
