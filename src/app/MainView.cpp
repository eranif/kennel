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
#include <random>
#include <wx/dir.h>
#include <wx/fontdlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/xrc/xmlres.h>

namespace {
static wxString kTerminalsGroupName = _("Terminals");
constexpr int kLineHeightSpacer = 2;

// Icon aliases for freshly created groups; one is picked at random and
// persisted so the group keeps its color across restarts.
constexpr const char *kGroupIconAliases[] = {
    "group-red",  "group-orange", "group-lime",   "group-green",  "group-teal",
    "group-cyan", "group-blue",   "group-indigo", "group-purple", "group-pink",
};

// Hands out icons from a shuffled bag so every colour is used once before
// any colour repeats; the bag is reshuffled once it runs dry.
wxString PickRandomGroupIcon() {
  static std::mt19937 rng{std::random_device{}()};
  static std::vector<wxString> bag;
  if (bag.empty()) {
    bag.assign(std::begin(kGroupIconAliases), std::end(kGroupIconAliases));
    std::shuffle(bag.begin(), bag.end(), rng);
  }
  wxString icon = bag.back();
  bag.pop_back();
  return icon;
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
} // namespace

MainView::MainView(wxWindow *parent)
    : MainViewBase(parent), m_registry(&AppManager::Get().Adapters()),
      m_workspace(&AppManager::Get().Workspace()),
      m_paths(AppManager::Get().Paths()) {

  // Does nothing on native impl (macOS & Linux).
  m_treeSessions->SetAlternateRowColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW).ChangeLightness(105));

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

  for (int i = 0; i < kSpinnerFrameCount; ++i) {
    wxString name;
    name.Printf("spinner-%d.svg", i);
    const wxString path = ResolveIconPath(name);
    if (!path.empty() && wxFileName::FileExists(path)) {
      m_spinnerFrames[i] = wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
    }
  }

  Bind(wxEVT_SESSION_IDLE, &MainView::OnSessionIdle, this);
  Bind(wxEVT_SESSION_ACTIVE, &MainView::OnSessionActive, this);
  Bind(wxEVT_SESSION_EXITED, &MainView::OnSessionExited, this);
  Bind(wxEVT_IDLE, &MainView::OnIdleEvent, this);
}

MainView::~MainView() {
  Unbind(wxEVT_SESSION_IDLE, &MainView::OnSessionIdle, this);
  Unbind(wxEVT_SESSION_ACTIVE, &MainView::OnSessionActive, this);
  Unbind(wxEVT_SESSION_EXITED, &MainView::OnSessionExited, this);
  Unbind(wxEVT_IDLE, &MainView::OnIdleEvent, this);
}

namespace {
// Icon shown on a session leaf: the agent's icon, or none for a plain
// terminal / an agent with no resolvable icon.
wxBitmapBundle SessionIconFor(const Session &session) {
  if (session.plainTerminal) {
    return wxBitmapBundle{};
  }
  const auto *agentDef = AppManager::Get().Adapters().FindAgent(session.agentName);
  if (agentDef == nullptr) {
    return wxBitmapBundle{};
  }
  const wxString path = ResolveIconPath(agentDef->iconPath);
  if (path.empty() || !wxFileExists(path)) {
    return wxBitmapBundle{};
  }
  return wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
}
} // namespace

SessionGroup *MainView::EnsureGroup(const wxString &groupName) {
  if (auto *existing = GetSessionGroup(groupName)) {
    return existing;
  }

  auto ownedGroup = std::make_unique<SessionGroup>(
      groupName, groupName == kTerminalsGroupName);
  auto *sessionGroup = ownedGroup.get();

  wxString iconAlias;
  if (sessionGroup->IsTerminalsGroup()) {
    iconAlias = "terminal";
  } else if (sessionGroup->IsDefaultGroup()) {
    iconAlias = "group-default";
  } else {
    iconAlias = m_workspace->GroupIcon(groupName);
    if (iconAlias.empty()) {
      iconAlias = PickRandomGroupIcon();
      m_workspace->SetGroupIcon(groupName, iconAlias);
      m_workspace->Persist();
    }
  }

  auto *itemData = new GroupItemData(std::move(ownedGroup));
  wxDataViewItem containerItem;
  if (sessionGroup->IsDefaultGroup()) {
    containerItem = m_treeSessions->PrependContainer(
        wxDataViewItem(), groupName, wxDataViewTreeCtrl::NO_IMAGE,
        wxDataViewTreeCtrl::NO_IMAGE, itemData);
  } else {
    containerItem = m_treeSessions->AppendContainer(
        wxDataViewItem(), groupName, wxDataViewTreeCtrl::NO_IMAGE,
        wxDataViewTreeCtrl::NO_IMAGE, itemData);
  }
  sessionGroup->SetContainerItem(containerItem);

  auto &bmps = AppManager::Get().GetBitmaps();
  m_treeSessions->SetItemIcon(containerItem, bmps.GetByAlias(iconAlias, false));
  m_treeSessions->Expand(containerItem);

  return sessionGroup;
}

GroupItemData *MainView::GetGroupItemData(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return nullptr;
  }
  return dynamic_cast<GroupItemData *>(m_treeSessions->GetItemData(item));
}

SessionItemData *MainView::GetSessionItemData(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return nullptr;
  }
  return dynamic_cast<SessionItemData *>(m_treeSessions->GetItemData(item));
}

SessionGroup *MainView::GetSessionGroup(const wxString &groupName) const {
  for (auto *group : GetAllGroups()) {
    if (group->GetGroupName() == groupName) {
      return group;
    }
  }
  return nullptr;
}

wxDataViewItem MainView::FindLeafItem(SessionGroup *group,
                                      SessionPage *page) const {
  if (group == nullptr || page == nullptr) {
    return wxDataViewItem{};
  }
  auto containerItem = group->GetContainerItem();
  const int count = m_treeSessions->GetChildCount(containerItem);
  for (int i = 0; i < count; ++i) {
    auto item = m_treeSessions->GetNthChild(containerItem, i);
    auto *data = GetSessionItemData(item);
    if (data && data->page == page) {
      return item;
    }
  }
  return wxDataViewItem{};
}

SessionPage *MainView::AddSession(SessionPage *page) {
  auto *group = EnsureGroup(page->GetSession().groupName);
  if (group == nullptr) {
    KLOG_ERROR() << "No agent group for '" << page->GetSession().groupName
                 << "'; session leaf not added";
    return nullptr;
  }

  if (!group->AddSession(page)) {
    return nullptr;
  }

  m_sessionsBook->AddPage(page, page->GetSession().name, false);

  auto leafItem = m_treeSessions->AppendItem(
      group->GetContainerItem(), page->GetSession().name,
      wxDataViewTreeCtrl::NO_IMAGE, new SessionItemData(page));
  auto bmp = SessionIconFor(page->GetSession());
  if (bmp.IsOk()) {
    m_treeSessions->SetItemIcon(leafItem, bmp);
  }
  return page;
}

void MainView::SelectSessionPage(SessionPage *page) {
  CHECK_NOT_NULL_RETURN(page);
  auto *group = GetSessionGroup(page->GetSession().groupName);
  CHECK_NOT_NULL_RETURN(group);

  auto leafItem = FindLeafItem(group, page);
  if (leafItem.IsOk()) {
    m_treeSessions->Select(leafItem);
  }

  int where = m_sessionsBook->FindPage(page);
  if (where != wxNOT_FOUND) {
    m_sessionsBook->SetSelection(where);
  }

  group->SetLastActive(page);
  page->CallAfter(&SessionPage::SetFocus);
  page->ApplyTitle();
}

void MainView::RestoreActiveSessionSelection() {
  auto *activePage = GetActiveSessionPage();
  if (activePage == nullptr) {
    return;
  }
  auto *group = GetSessionGroup(activePage->GetSession().groupName);
  auto leafItem = FindLeafItem(group, activePage);
  if (leafItem.IsOk()) {
    m_treeSessions->Select(leafItem);
  }
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

SessionPage *MainView::AddSessionPage(const Session &session, bool resume) {
  auto *group = EnsureGroup(session.groupName);
  if (group == nullptr) {
    return nullptr;
  }

  std::optional<AgentDef> agent{std::nullopt};
  if (group->IsSessionGroup()) {
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

bool MainView::LaunchSession(const NewSessionRequest &req) {
  StatusOr<Session> session = m_workspace->Create(req);
  if (!session.ok()) {
    wxMessageBox(session.status().message(), "Launch failed",
                 wxOK | wxICON_ERROR, this);
    return false;
  }

  auto *page = AddSessionPage(*session, req.resume);
  if (page == nullptr) {
    KLOG_INFO() << "Session creation failed";
    if (auto *group = GetSessionGroup(session->groupName)) {
      RemoveGroupIfEmpty(group->GetContainerItem());
    }
    return false;
  }

  SelectSessionPage(page);

  if (Status st = m_workspace->Persist(); !st.ok()) {
    KLOG_WARN() << "Session created but workspace not persisted: "
                << st.message();
  }

  auto &prefs = AppManager::Get().GetPrefs();
  PushRecent(prefs.recentWorkingDirs, req.workingDir);
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not persist recent working dirs: " << st.message();
  }
  return true;
}

void MainView::RestoreSessions() {
  const auto &sessions = m_workspace->Sessions();
  if (sessions.empty()) {
    return;
  }

  int restored = 0;
  for (const Session &s : sessions) {
    auto *page = AddSessionPage(s, true);
    if (page) {
      page->GetTerminal()->EnsureStarted();
      ++restored;
    }
  }

  KLOG_INFO() << "Restored " << restored << " session(s)";
  if (GroupCount() > 0) {
    DoSelectGroup(m_treeSessions->GetNthChild(wxDataViewItem(), 0));
  }
}

void MainView::DoSelectGroup(const wxString &name) {
  auto *group = GetSessionGroup(name);
  CHECK_NOT_NULL_RETURN(group);
  DoSelectGroup(group->GetContainerItem());
}

void MainView::DoSelectGroup(const wxDataViewItem &item) {
  CHECK_ITEM_RETURN(item);
  auto *data = GetGroupItemData(item);
  CHECK_NOT_NULL_RETURN(data);
  auto *group = data->group.get();

  m_treeSessions->Select(item);
  if (group->IsEmpty()) {
    return;
  }

  auto *target = group->GetLastActive();
  if (target == nullptr) {
    target = group->GetSessions().front();
  }
  SelectSessionPage(target);
}

void MainView::OnSelectionChanged(wxDataViewEvent &event) {
  auto item = event.GetItem();
  CHECK_ITEM_RETURN(item);

  if (auto *sessionData = GetSessionItemData(item)) {
    SelectSessionPage(sessionData->page);
    return;
  }

  // Clicking a group node only toggles its expand/collapse state; it must
  // not change which session is selected/shown.
  if (m_treeSessions->IsExpanded(item)) {
    m_treeSessions->Collapse(item);
  } else {
    m_treeSessions->Expand(item);
  }
  RestoreActiveSessionSelection();
}

void MainView::ApplyFont(const wxFont &f) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetFont(f);
  if (!active) {
    return;
  }
  for (auto *page : GetAllSessions()) {
    page->ApplyTheme(*active);
    page->GetTerminal()->SendSizeEvent();
  }
  m_sessionsBook->SendSizeEvent();
  KLOG_INFO() << "Applied terminal font '" << f.GetFaceName() << "' to "
              << static_cast<int>(SessionCount()) << " terminal(s)";
  SavePrefs();
}

void MainView::ApplyOptimizedDrawing() {
  bool optimized = AppManager::Get().GetPrefs().terminalOptimizedDrawing;
  for (auto *page : GetAllSessions()) {
    page->GetTerminal()->EnableSafeDrawing(!optimized);
    page->GetTerminal()->Refresh();
  }
}

void MainView::ApplyPrefs() {
  const auto &prefs = AppManager::Get().GetPrefs();
  ApplyTheme(prefs.terminalTheme);
  wxFont font;
  font.SetNativeFontInfo(prefs.terminalFontDesc);
  ApplyFont(font);
  ApplyOptimizedDrawing();

  for (auto *page : GetAllSessions()) {
    page->GetTerminal()->SetBufferSize(prefs.scrollbackLines);
  }
}

void MainView::ApplyTheme(const wxString &themeName) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetTheme(themeName);
  if (!active) {
    return;
  }
  for (auto *page : GetAllSessions()) {
    page->ApplyTheme(*active);
    page->GetTerminal()->SendSizeEvent();
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

SessionGroup *MainView::GetSelectedGroup() const {
  auto item = m_treeSessions->GetSelection();
  if (!item.IsOk()) {
    return nullptr;
  }

  if (auto *groupData = GetGroupItemData(item)) {
    return groupData->group.get();
  }
  if (GetSessionItemData(item)) {
    auto *parentData = GetGroupItemData(m_treeSessions->GetItemParent(item));
    return parentData ? parentData->group.get() : nullptr;
  }
  return nullptr;
}

SessionPage *MainView::GetActiveSessionPage() const {
  return dynamic_cast<SessionPage *>(m_sessionsBook->GetCurrentPage());
}

void MainView::RefreshCurrentSelection() {
  auto *group = GetSelectedGroup();
  CHECK_NOT_NULL_RETURN(group);
  auto *page = GetActiveSessionPage();
  if (group->IsSessionGroup() && page != nullptr) {
    page->Restart();
  }
}

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

void MainView::RefreshSelectedGroup() {
  auto *group = GetSelectedGroup();
  if (group == nullptr || group->IsTerminalsGroup()) {
    return;
  }

  group->Apply([this](SessionPage *page) {
    page->CallAfter(&SessionPage::Restart);
    m_pendingIdle++;
  });
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
  m_sessionsBook->DeleteAllPages();
  m_treeSessions->DeleteAllItems();
  m_workspace->CloseAll();
  m_workspace->Persist();
}

void MainView::DeleteGroupByName(const wxString &name) {
  auto *group = GetSessionGroup(name);
  CHECK_NOT_NULL_RETURN(group);

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

  for (auto *page : group->GetSessions()) {
    int where = m_sessionsBook->FindPage(page);
    if (where != wxNOT_FOUND) {
      m_sessionsBook->DeletePage(where);
    }
  }

  m_workspace->CloseGroup(name);
  m_workspace->Persist();

  auto containerItem = group->GetContainerItem();
  m_treeSessions->DeleteItem(containerItem); // deletes GroupItemData -> group

  if (GroupCount() > 0) {
    DoSelectGroup(m_treeSessions->GetNthChild(wxDataViewItem(), 0));
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

  if (Status st = m_workspace->RenameGroup(oldName, newName); !st.ok()) {
    wxMessageBox(st.message(), "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }
  m_workspace->Persist();

  group->SetGroupName(newName);
  m_treeSessions->SetItemText(group->GetContainerItem(), newName);
}

void MainView::RenameSession(SessionPage *page) {
  CHECK_NOT_NULL_RETURN(page);
  const wxString oldName = page->GetSession().name;
  wxString newName =
      ::wxGetTextFromUser(_("New name:"), "Kennel", oldName, this);
  if (newName.empty() || newName == oldName) {
    return;
  }

  if (IsNameExist(newName)) {
    wxMessageBox(_("A session with this name already exists"), "Kennel",
                 wxICON_WARNING | wxOK | wxCENTER, this);
    return;
  }

  if (Status st = m_workspace->Rename(oldName, newName); !st.ok()) {
    wxMessageBox(st.message(), "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }
  m_workspace->Persist();

  auto *group = GetSessionGroup(page->GetSession().groupName);
  page->GetSession().name = newName;
  page->SetDefaultSessionName(newName);

  if (group) {
    auto leafItem = FindLeafItem(group, page);
    if (leafItem.IsOk()) {
      m_treeSessions->SetItemText(leafItem, newName);
    }
  }
}

void MainView::RenameItem() {
  auto item = m_treeSessions->GetSelection();
  CHECK_ITEM_RETURN(item);

  if (auto *sessionData = GetSessionItemData(item)) {
    RenameSession(sessionData->page);
    return;
  }
  if (auto *groupData = GetGroupItemData(item)) {
    RenameGroup(groupData->group.get());
  }
}

void MainView::SelectFallbackSession(SessionGroup *preferredGroup) {
  if (preferredGroup && !preferredGroup->IsEmpty()) {
    SelectSessionPage(preferredGroup->GetSessions().front());
    return;
  }
  for (auto *group : GetAllGroups()) {
    if (!group->IsEmpty()) {
      SelectSessionPage(group->GetSessions().front());
      return;
    }
  }
}

void MainView::CloseSession(SessionGroup *group, const wxString &sessionName) {
  CHECK_NOT_NULL_RETURN(group);
  auto *page = group->GetSessionByName(sessionName);
  CHECK_NOT_NULL_RETURN(page);

  bool wasActive = (GetActiveSessionPage() == page);
  auto leafItem = FindLeafItem(group, page);
  group->RemoveSession(sessionName);
  if (leafItem.IsOk()) {
    m_treeSessions->DeleteItem(leafItem);
  }

  int where = m_sessionsBook->FindPage(page);
  if (where != wxNOT_FOUND) {
    m_sessionsBook->DeletePage(where); // destroys the SessionPage window
  }

  m_workspace->CloseSession(sessionName);
  m_workspace->Persist();

  if (wasActive) {
    SelectFallbackSession(group->IsEmpty() ? nullptr : group);
  }

  RemoveGroupIfEmpty(group->GetContainerItem());
}

void MainView::RefreshGroup(SessionGroup *group) {
  CHECK_NOT_NULL_RETURN(group);
  if (group->IsTerminalsGroup()) {
    return;
  }
  group->Apply([this](SessionPage *page) {
    page->CallAfter(&SessionPage::Restart);
    m_pendingIdle++;
  });
  if (m_pendingIdle > 0) {
    GetMainFrame()->SetActivityText(
        wxString::Format(_("Refreshing %d sessions"), m_pendingIdle));
    GetMainFrame()->StartActivityIndicator();
  }
}

void MainView::DoGroupMenu(const wxDataViewItem &item) {
  auto *data = GetGroupItemData(item);
  CHECK_NOT_NULL_RETURN(data);

  auto *group = data->group.get();
  if (group->IsTerminalsGroup()) {
    wxMenu menu;
    menu.Append(wxID_ADD, _("New Terminal..."));
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { StartTerminal(); }, wxID_ADD);
    m_treeSessions->PopupMenu(&menu);
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
    if (group->IsDefaultGroup()) {
      menu.Enable(XRCID("rename-group"), false);
    }

    menu.Bind(
        wxEVT_MENU,
        [group, this](wxCommandEvent &) {
          StartAgent(wxEmptyString, group->GetGroupName());
        },
        wxID_ADD);

    menu.Bind(
        wxEVT_MENU, [group, this](wxCommandEvent &) { RenameGroup(group); },
        XRCID("rename-group"));

    menu.Bind(
        wxEVT_MENU,
        [group, this](wxCommandEvent &) {
          CallAfter(&MainView::DeleteGroupByName, group->GetGroupName());
        },
        wxID_CLOSE_ALL);

    menu.Bind(
        wxEVT_MENU, [group, this](wxCommandEvent &) { RefreshGroup(group); },
        XRCID("refresh-sessions"));
    m_treeSessions->PopupMenu(&menu);
  }
}

void MainView::DoSessionMenu(const wxDataViewItem &item) {
  auto *sessionData = GetSessionItemData(item);
  CHECK_NOT_NULL_RETURN(sessionData);
  auto *page = sessionData->page;

  auto parentItem = m_treeSessions->GetItemParent(item);
  auto *groupData = GetGroupItemData(parentItem);
  CHECK_NOT_NULL_RETURN(groupData);
  auto *group = groupData->group.get();

  wxString sessionName = page->GetSession().name;
  wxMenu menu;
  menu.Append(XRCID("session-group-close-session"), _("Close"),
              _("Close Session"));
  menu.Bind(
      wxEVT_MENU,
      [group, sessionName, this](wxCommandEvent &) {
        CloseSession(group, sessionName);
      },
      XRCID("session-group-close-session"));

  menu.AppendSeparator();
  if (page->IsPlainTerminal()) {
    menu.Append(XRCID("rename-terminal"), _("Rename Terminal"),
                _("Rename Terminal"));
    menu.Bind(
        wxEVT_MENU, [page, this](wxCommandEvent &) { RenameSession(page); },
        XRCID("rename-terminal"));
  } else {
    wxMenu *moveMenu = new wxMenu;
    wxString currentGroupName = group->GetGroupName();
    auto groups = AppManager::Get().Groups(
        [currentGroupName](const Session &sess) -> bool {
          if (sess.plainTerminal)
            return false;
          if (sess.groupName == currentGroupName)
            return false;
          return true;
        });

    if (!groups.empty()) {
      for (const wxString &groupName : groups) {
        int id = wxXmlResource::GetXRCID(
            wxString::Format("move-to-group-%s", groupName));
        moveMenu->Append(id, groupName,
                         wxString::Format(_("Move to group: %s"), groupName));
        moveMenu->Bind(
            wxEVT_MENU,
            [groupName, sessionName, currentGroupName, this](wxCommandEvent &) {
              MoveSessionToGroup(sessionName, currentGroupName, groupName);
            },
            id);
      }
      moveMenu->AppendSeparator();
    }
    moveMenu->Append(XRCID("create-new-group"), _("New Group..."));
    moveMenu->Bind(
        wxEVT_MENU,
        [sessionName, currentGroupName, this](wxCommandEvent &) {
          wxString newGroup = ::wxGetTextFromUser(
              _("New Group Name"), "Kennel", wxEmptyString, this);
          if (newGroup.empty() || newGroup == currentGroupName)
            return;
          MoveSessionToGroup(sessionName, currentGroupName, newGroup);
        },
        XRCID("create-new-group"));
    menu.AppendSubMenu(moveMenu, _("Move To Group"));
  }
  m_treeSessions->PopupMenu(&menu);
}

std::vector<SessionPage *> MainView::GetAllSessions() const {
  std::vector<SessionPage *> result;
  for (auto *group : GetAllGroups()) {
    const auto &sessions = group->GetSessions();
    result.insert(result.end(), sessions.begin(), sessions.end());
  }
  return result;
}

std::vector<SessionGroup *> MainView::GetAllGroups() const {
  std::vector<SessionGroup *> result;
  const wxDataViewItem root;
  const int count = m_treeSessions->GetChildCount(root);
  for (int i = 0; i < count; ++i) {
    auto *data = GetGroupItemData(m_treeSessions->GetNthChild(root, i));
    if (data) {
      result.push_back(data->group.get());
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
  auto checkIfNameExists = [&name, &matchFound](SessionPage *page) {
    if (page->IsPlainTerminal())
      return true; // continue
    if (page->GetSession().name == name) {
      matchFound = true;
      return false;
    }
    return true;
  };
  Traverse(checkIfNameExists);
  return matchFound;
}

void MainView::RemoveGroupIfEmpty(const wxDataViewItem &item) {
  auto *data = GetGroupItemData(item);
  CHECK_NOT_NULL_RETURN(data);
  auto *group = data->group.get();

  if (!group->IsEmpty() || group->IsDefaultGroup()) {
    return;
  }

  m_treeSessions->DeleteItem(item);
}

void MainView::OnContextMenu(wxDataViewEvent &event) {
  auto item = event.GetItem();
  CHECK_ITEM_RETURN(item);
  if (m_treeSessions->IsContainer(item)) {
    DoGroupMenu(item);
  } else {
    DoSessionMenu(item);
  }
}

void MainView::SelectSession(const wxString &sessionName) {
  auto *group = GetSelectedGroup();
  CHECK_NOT_NULL_RETURN(group);
  auto *page = group->GetSessionByName(sessionName);
  CHECK_NOT_NULL_RETURN(page);
  SelectSessionPage(page);
}

size_t MainView::GroupCount() const {
  return static_cast<size_t>(m_treeSessions->GetChildCount(wxDataViewItem()));
}

size_t MainView::SessionCount() const {
  size_t count{0};
  for (auto *group : GetAllGroups()) {
    count += group->GetCount();
  }
  return count;
}

void MainView::SelectSession(bool forward) {
  auto sessions = GetAllSessions();
  if (sessions.size() <= 1) {
    return;
  }

  auto *current = GetActiveSessionPage();
  int where = -1;
  for (size_t i = 0; i < sessions.size(); ++i) {
    if (sessions[i] == current) {
      where = static_cast<int>(i);
      break;
    }
  }
  if (where == -1) {
    where = 0;
  }

  const int count = static_cast<int>(sessions.size());
  where = forward ? (where + 1) % count : (where - 1 + count) % count;
  SelectSessionPage(sessions[where]);
}

void MainView::MoveSessionToGroup(const wxString &sessionName,
                                  const wxString &fromGroupName,
                                  const wxString &toGroupName) {
  KLOG_DEBUG() << "Moving session: " << sessionName << " from: "
              << fromGroupName << "->" << toGroupName;

  auto *oldGroup = GetSessionGroup(fromGroupName);
  CHECK_NOT_NULL_RETURN(oldGroup);

  auto *page = oldGroup->GetSessionByName(sessionName);
  CHECK_NOT_NULL_RETURN(page);

  bool wasActive = (GetActiveSessionPage() == page);
  auto oldLeafItem = FindLeafItem(oldGroup, page);
  oldGroup->RemoveSession(sessionName);
  if (oldLeafItem.IsOk()) {
    m_treeSessions->DeleteItem(oldLeafItem);
  }

  auto *newGroup = EnsureGroup(toGroupName);
  CHECK_NOT_NULL_RETURN(newGroup);

  page->GetSession().groupName = toGroupName;
  newGroup->AddSession(page);

  auto leafItem = m_treeSessions->AppendItem(
      newGroup->GetContainerItem(), sessionName, wxDataViewTreeCtrl::NO_IMAGE,
      new SessionItemData(page));
  auto bmp = SessionIconFor(page->GetSession());
  if (bmp.IsOk()) {
    m_treeSessions->SetItemIcon(leafItem, bmp);
  }

  if (Status st = m_workspace->MoveSession(sessionName, toGroupName); !st.ok()) {
    wxMessageBox(st.message(), "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }
  m_workspace->Persist();

  if (wasActive) {
    SelectSessionPage(page);
  }

  RemoveGroupIfEmpty(oldGroup->GetContainerItem());
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
  wxString name = e.GetString();
  for (auto *group : GetAllGroups()) {
    if (group->FindByName(name) != wxNOT_FOUND) {
      CloseSession(group, name);
      return;
    }
  }
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

  bmps.Load("restart.svg");
  bmps.AddAlias("restart.svg", "restart");

  bmps.Load("new.svg");
  bmps.AddAlias("new.svg", "new");

  bmps.Load("agent.svg");
  bmps.AddAlias("agent.svg", "agent");

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
