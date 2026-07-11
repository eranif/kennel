#include "MainView.hpp"

#include "MainFrame.h"
#include "StartAgentDialog.hpp"
#include "ThemeLoader.h"
#include "ThemeManager.h"
#include "app/AssetBootstrap.h"
#include "app/SessionGroup.h"
#include "app/SessionPage.hpp"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/Logger.h"
#include "core/WorkspaceManager.h"

#include "terminal_view.h"

#include "core/Helpers.h"
#include <algorithm>
#include <wx/dir.h>
#include <wx/fontdlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>

namespace {
static wxString kTerminalsGroupName = _("Terminals");
constexpr int kLineHeightSpacer = 2;

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
} // namespace

MainView::MainView(wxWindow *parent)
    : MainViewBase(parent), m_registry(&AppManager::Get().Adapters()),
      m_workspace(&AppManager::Get().Workspace()),
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

  auto &bmps = AppManager::Get().GetBitmaps();
  LoadBitmaps();

  for (int i = 0; i < kSpinnerFrameCount; ++i) {
    wxString name;
    name.Printf("spinner-%d.svg", i);
    const wxString path = ResolveIconPath(name);
    if (!path.empty() && wxFileName::FileExists(path)) {
      m_spinnerFrames[i] = wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
    }
  }
  Bind(wxEVT_GROUP_PAGE_CHANGED, &MainView::OnGroupPageChanged, this);
  Bind(wxEVT_GROUP_LAST_PAGE_CLOSED, &MainView::OnGroupLastPageClosed, this);
}

SessionGroup *MainView::EnsureGroup(const wxString &groupName) {
  // Return existing node if already present.
  auto *sessionGroup = GetSessionGroup(groupName);
  if (sessionGroup != nullptr) {
    return sessionGroup;
  }

  auto &bmps = AppManager::Get().GetBitmaps();
  sessionGroup = new SessionGroup(m_sessionsBook, groupName,
                                  groupName == kTerminalsGroupName);
  m_sessionsBook->AddPage(sessionGroup, groupName, true);

  wxDataViewIconText icontext(
      groupName,
      bmps.GetByAlias(sessionGroup->IsTerminalsGroup() ? "terminal" : "folder",
                      false));
  wxVector<wxVariant> cols;
  wxVariant v;
  v << icontext;
  cols.push_back(v);
  m_dvListCtrlGroups->AppendItem(
      cols,
      reinterpret_cast<wxUIntPtr>(new GroupItemData(groupName, sessionGroup)));
  return sessionGroup;
}

SessionGroup *MainView::GetSessionGroup(int row) const {
  if (row < 0 || row >= static_cast<int>(m_dvListCtrlGroups->GetItemCount()))
    return nullptr;
  auto item = m_dvListCtrlGroups->RowToItem(row);
  auto cd = GetGroupItemData(item);
  if (cd) {
    return cd->groupPage;
  }
  return nullptr;
}

SessionGroup *MainView::GetSessionGroup(const wxString &groupName) const {
  const int count = m_dvListCtrlGroups->GetItemCount();
  for (int i = 0; i < count; ++i) {
    auto item = m_dvListCtrlGroups->RowToItem(i);
    auto cd = GetGroupItemData(item);
    if (cd && cd->groupName == groupName) {
      return cd->groupPage;
    }
  }
  return nullptr;
}

wxDataViewItem MainView::GetSessionGroupItem(const wxString &name) {
  const int count = m_dvListCtrlGroups->GetItemCount();
  for (int i = 0; i < count; ++i) {
    auto item = m_dvListCtrlGroups->RowToItem(i);
    auto cd = GetGroupItemData(item);
    if (cd && cd->groupName == name) {
      return item;
    }
  }
  return wxDataViewItem{nullptr};
}

GroupItemData *MainView::GetGroupItemData(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return nullptr;
  }
  auto *groupItemData =
      reinterpret_cast<GroupItemData *>(m_dvListCtrlGroups->GetItemData(item));
  return groupItemData;
}

SessionPage *MainView::AddSession(SessionPage *page) {
  auto *group = EnsureGroup(page->GetSession().groupName);
  if (group == nullptr) {
    KLOG_ERROR() << "No agent group for '" << page->GetSession().groupName
                 << "'; session leaf not added";
    return nullptr;
  }

  group->AddSessionPage(page);
  return page;
}

MainView::~MainView() {
  Unbind(wxEVT_GROUP_PAGE_CHANGED, &MainView::OnGroupPageChanged, this);
  Unbind(wxEVT_GROUP_LAST_PAGE_CLOSED, &MainView::OnGroupLastPageClosed, this);
}

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

void MainView::StartAgent(const wxString &agentName,
                          const wxString &groupName) {
  StartAgentDialog dlg(this);
  if (!agentName.empty()) {
    dlg.SetSelectedClientName(agentName);
  }
  if (!groupName.empty()) {
    dlg.SetSelectedGroup(groupName);
  }
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  LaunchSession(dlg.GetRequest());
}

bool MainView::LaunchSession(const NewSessionRequest &req) {
  StatusOr<Session> session = m_workspace->Create(req);
  if (!session.ok()) {
    wxMessageBox(session.status().message(), "Launch failed",
                 wxOK | wxICON_ERROR, this);
    return false;
  }

  if (Status st = m_workspace->Persist(); !st.ok()) {
    KLOG_WARN() << "Session created but workspace not persisted: "
                << st.message();
  }

  auto *sessionGroup = EnsureGroup(session->groupName);
  sessionGroup->NewSessionPage(*session, req.resume);

  auto &prefs = AppManager::Get().GetPrefs();
  PushRecent(prefs.recentWorkingDirs, req.workingDir);
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not persist recent working dirs: " << st.message();
  }
  return true;
}

SessionPage *MainView::AddSessionPage(const Session &session, bool resume) {
  auto *group = EnsureGroup(session.groupName);
  if (!group) {
    return nullptr;
  }
  return group->NewSessionPage(session, resume);
}

void MainView::RestoreSessions() {
  const auto &sessions = m_workspace->Sessions();
  if (sessions.empty()) {
    return;
  }

  const auto &prefs = AppManager::Get().GetPrefs();
  int restored = 0;

  for (const Session &s : sessions) {
    auto *page = AddSessionPage(s, true);
    if (page) {
      page->GetTerminal()->EnsureStarted();
      ++restored;
    }
  }
  KLOG_INFO() << "Restored " << restored << " session(s)";
  if (m_dvListCtrlGroups->GetItemCount() > 0) {
    m_dvListCtrlGroups->SelectRow(0);
  }
}

void MainView::DoSelectGroup(const wxString &name) {
  auto item = GetSessionGroupItem(name);
  CHECK_ITEM_RETURN(item);
  DoSelectGroup(item);
}

void MainView::DoSelectGroup(const wxDataViewItem &item) {
  CHECK_ITEM_RETURN(item);
  auto *cd = GetGroupItemData(item);
  CHECK_NOT_NULL_RETURN(cd);

  auto *group = GetSessionGroup(cd->groupName);
  CHECK_NOT_NULL_RETURN(group);

  m_dvListCtrlGroups->SelectRow(m_dvListCtrlGroups->ItemToRow(item));

  for (size_t i = 0; i < m_sessionsBook->GetPageCount(); ++i) {
    if (m_sessionsBook->GetPage(i) == group) {
      m_sessionsBook->SetSelection(i);
      auto *activeSession = group->GetActivePage();
      if (activeSession) {
        activeSession->CallAfter(&SessionPage::SetFocus);
      }
      return;
    }
  }
}

void MainView::OnSelectionChanged(wxDataViewEvent &event) {
  DoSelectGroup(event.GetItem());
}

void MainView::ApplyFont(const wxFont &f) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetFont(f);
  if (!active) {
    return;
  }
  for (size_t i = 0; i < m_sessionsBook->GetPageCount(); ++i) {
    auto *sg = dynamic_cast<SessionGroup *>(m_sessionsBook->GetPage(i));
    if (sg) {
      sg->ApplyFont(f);
    }
  }
  KLOG_INFO() << "Applied terminal font '" << f.GetFaceName() << "' to "
              << static_cast<int>(SessionCount()) << " terminal(s)";
  SavePrefs();
}

void MainView::ApplyOptimizedDrawing() {
  for (size_t i = 0; i < m_sessionsBook->GetPageCount(); ++i) {
    auto *sg = dynamic_cast<SessionGroup *>(m_sessionsBook->GetPage(i));
    if (sg) {
      sg->ApplyOptimizedDrawing();
    }
  }
}

void MainView::ApplyTheme(const wxString &themeName) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetTheme(themeName);
  if (!active) {
    return;
  }
  for (size_t i = 0; i < m_sessionsBook->GetPageCount(); ++i) {
    auto *sg = dynamic_cast<SessionGroup *>(m_sessionsBook->GetPage(i));
    if (sg) {
      sg->ApplyTheme(themeName);
    }
  }
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

SessionGroup *MainView::GetSelectedGroup() const {
  auto item = m_dvListCtrlGroups->GetSelection();
  if (!item.IsOk())
    return nullptr;
  auto *cd = GetGroupItemData(item);
  if (cd == nullptr)
    return nullptr;
  return cd->groupPage;
}

void MainView::RefreshCurrentSelection() {
  auto *group = GetSelectedGroup();
  CHECK_NOT_NULL_RETURN(group);
  if (group->IsSessionGroup() && group->GetActivePage()) {
    group->GetActivePage()->Restart();
  }
}

bool MainView::CanRefreshCurrent() const {
  auto *group = GetSelectedGroup();
  return group && group->IsSessionGroup() && group->GetActivePage() != nullptr;
}

bool MainView::IsSelectionSessionGroup() const {
  auto *group = GetSelectedGroup();
  if (group == nullptr)
    return false;
  return group && group->IsSessionGroup();
}

bool MainView::IsSelectionTerminalGroup() const {
  auto *group = GetSelectedGroup();
  if (group == nullptr)
    return false;
  return group && group->IsTerminalsGroup();
}

void MainView::RefreshSelectedGroup() {
  auto *group = GetSelectedGroup();
  if (group && group->IsSessionGroup()) {
    group->RefreshAll();
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
  for (size_t i = 0; i < m_dvListCtrlGroups->GetItemCount(); ++i) {
    auto *group = GetSessionGroup(i);
    wxDELETE(group);
  }
  m_dvListCtrlGroups->DeleteAllItems();
  m_sessionsBook->DeleteAllPages();
  m_workspace->CloseAll();
  m_workspace->Persist();
}

void MainView::DeleteGroupByName(const wxString &name) {
  KLOG_INFO() << "Closing group: " << name;
  auto *group = GetSessionGroup(name);
  CHECK_NOT_NULL_RETURN(group);
  KLOG_INFO() << "Find SessionGroup pointer";

  auto item = GetSessionGroupItem(name);
  CHECK_ITEM_RETURN(item);
  KLOG_INFO() << "Find SessionGroup item in tree";

  if (!group->IsEmpty()) {
    wxString msg;
    msg << _("This will close ") << group->GetCount()
        << _(" session(s).\nContinue?");
    if (wxMessageBox(msg, "Kennel",
                     wxICON_WARNING | wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT) !=
        wxYES) {
      return;
    }
  }

  KLOG_INFO() << "Closing group: " << group->GetCount() << " sessions";

  // Delete the notebook page
  int where = m_sessionsBook->FindPage(group);
  if (where == wxNOT_FOUND) {
    KLOG_INFO() << "Couldn't find page for group: " << group->GetCount();
    return;
  }
  m_sessionsBook->DeletePage(where);

  // Delete the list view entry
  auto *cd = GetGroupItemData(item);
  wxDELETE(cd);

  m_dvListCtrlGroups->DeleteItem(m_dvListCtrlGroups->ItemToRow(item));

  m_workspace->CloseGroup(name);
  m_workspace->Persist();

  if (m_dvListCtrlGroups->GetItemCount() > 0) {
    DoSelectGroup(m_dvListCtrlGroups->RowToItem(0));
  } else {
    wxTheApp->GetTopWindow()->SetLabel(_("Kennel"));
  }
}

void MainView::DoGroupMenu(const wxDataViewItem &item) {
  auto *data = GetGroupItemData(item);
  CHECK_NOT_NULL_RETURN(data);

  auto *group = data->groupPage;
  if (group->IsTerminalsGroup()) {
    wxMenu menu;
    menu.Append(wxID_ADD, _("New Terminal..."));
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { StartTerminal(); }, wxID_ADD);
    m_dvListCtrlGroups->PopupMenu(&menu);
  } else {
    wxMenu menu;
    menu.Append(wxID_ADD, _("Start Agent..."));
    menu.AppendSeparator();
    menu.Append(XRCID("rename-group"), _("Rename Group..."));
    menu.AppendSeparator();
    menu.Append(wxID_CLOSE_ALL, _("Close Group"));
    menu.AppendSeparator();
    menu.Append(XRCID("refresh-sessions"), _("Refresh"));

    // The "Default" group must always exist and cannot be renamed.
    if (!group->IsDefaultGroup()) {
      menu.Enable(XRCID("rename-group"), false);
    }

    menu.Bind(
        wxEVT_MENU,
        [group, this](wxCommandEvent &) {
          StartAgent(wxEmptyString, group->GetGroupName());
        },
        wxID_ADD);

    menu.Bind(
        wxEVT_MENU, [group](wxCommandEvent &) { group->Rename(); },
        XRCID("rename-group"));

    menu.Bind(
        wxEVT_MENU,
        [group, this](wxCommandEvent &) {
          CallAfter(&MainView::DeleteGroupByName, group->GetGroupName());
        },
        wxID_CLOSE_ALL);

    menu.Bind(
        wxEVT_MENU, [group](wxCommandEvent &) { group->RefreshAll(); },
        XRCID("refresh-sessions"));
    m_dvListCtrlGroups->PopupMenu(&menu);
  }
}

std::vector<SessionPage *> MainView::GetAllSessions() const {
  std::vector<SessionPage *> result;
  for (size_t i = 0; i < m_dvListCtrlGroups->GetItemCount(); ++i) {
    auto *group = GetSessionGroup(i);
    if (group) {
      auto v = group->GetAllSessions();
      result.insert(result.end(), v.begin(), v.end());
    }
  }
  return result;
}

std::vector<SessionGroup *> MainView::GetAllGroups() const {
  std::vector<SessionGroup *> result;
  for (size_t i = 0; i < m_dvListCtrlGroups->GetItemCount(); ++i) {
    auto *group = GetSessionGroup(i);
    if (group) {
      result.push_back(group);
    }
  }
  return result;
}

void MainView::Traverse(std::function<bool(SessionPage *)> visit) const {
  auto all = GetAllSessions();
  for (auto *session : all) {
    if (!visit(session))
      return;
  }
}

bool MainView::IsNameExist(const wxString &name) const {
  bool matchFound{false};
  auto checkIfNameExists = [&name, &matchFound, this](SessionPage *page) {
    if (page->GetSession().name == name) {
      matchFound = true;
      return false;
    }
    return true;
  };
  Traverse(checkIfNameExists);
  return matchFound;
}

void MainView::RenameSelectedGroup() {
  auto *group = GetSelectedGroup();
  CHECK_NOT_NULL_RETURN(group);
  group->Rename();
}

void MainView::RemoveGroupIfEmpty(const wxDataViewItem &item) {
  const auto row = m_dvListCtrlGroups->ItemToRow(item);
  auto *group = GetSessionGroup(row);
  CHECK_NOT_NULL_RETURN(group);

  if (group->IsEmpty() || group->IsDefaultGroup()) {
    return;
  }

  m_sessionsBook->DeletePage(m_sessionsBook->FindPage(group));
  m_dvListCtrlGroups->DeleteItem(row);
}

void MainView::OnContextMenu(wxDataViewEvent &event) {
  DoGroupMenu(event.GetItem());
}

SessionPage *MainView::GetActiveTerminal() {
  if (m_sessionsBook->GetSelection() == wxNOT_FOUND) {
    return nullptr;
  }
  return dynamic_cast<SessionPage *>(
      m_sessionsBook->GetPage(m_sessionsBook->GetSelection()));
}

void MainView::SelectSession(const wxString &sessionName) {
  auto *group = GetSelectedGroup();
  CHECK_NOT_NULL_RETURN(group);
  group->SelectSession(sessionName);
}

size_t MainView::SessionCount() const {
  size_t count{0};
  for (size_t i = 0; i < m_dvListCtrlGroups->GetItemCount(); ++i) {
    auto *grp = GetSessionGroup(i);
    if (grp) {
      count += grp->GetCount();
    }
  }
  return count;
}

void MainView::SelectSession(bool forward) {
  auto *group = GetSelectedGroup();
  CHECK_NOT_NULL_RETURN(group);
  group->SelectSession(forward);
}

void MainView::LoadBitmaps() {
  auto &bmps = AppManager::Get().GetBitmaps();
  bmps.Load("home.svg");
  bmps.AddAlias("home.svg", "home");

  bmps.Load("folder.svg");
  bmps.AddAlias("folder.svg", "folder");

  bmps.Load("folder-open.svg");
  bmps.AddAlias("folder-open.svg", "folder-open");

  bmps.Load("up.svg");
  bmps.AddAlias("up.svg", "up");

  bmps.Load("terminal.svg");
  bmps.AddAlias("terminal.svg", "terminal");

  bmps.Load("restart.svg");
  bmps.AddAlias("restart.svg", "restart");

  bmps.Load("new.svg");
  bmps.AddAlias("new.svg", "new");

  bmps.Load("agent.svg");
  bmps.AddAlias("agent.svg", "agent");

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

void MainView::OnGroupPageChanged(wxCommandEvent &event) {
  event.Skip();
  KLOG_DEBUG() << "Page Changed event from group: " << event.GetString();
  DoSelectGroup(event.GetString());
}

void MainView::OnGroupLastPageClosed(wxCommandEvent &event) {
  event.Skip();
  KLOG_DEBUG() << "Last Page Closed event from group: " << event.GetString();
  DeleteGroupByName(event.GetString());
}
