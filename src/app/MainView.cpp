#include "MainView.hpp"

#include "MainFrame.h"
#include "SessionPage.hpp"
#include "StartAgentDialog.hpp"
#include "ThemeLoader.h"
#include "ThemeManager.h"
#include "app/AssetBootstrap.h"
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

  BuildTree();

  Bind(wxEVT_SESSION_IDLE, &MainView::OnSessionIdle, this);
  Bind(wxEVT_SESSION_ACTIVE, &MainView::OnSessionActive, this);
  Bind(wxEVT_SESSION_EXITED, &MainView::OnSessionExited, this);
  Bind(wxEVT_IDLE, &MainView::OnIdleEvent, this);
}

void MainView::BuildTree() {
  if (m_dvListCtrlSessions == nullptr) {
    long treeStyle = wxDV_ROW_LINES | wxDV_SINGLE | wxDV_NO_HEADER;
#ifdef __WXMSW__
    treeStyle &= ~wxDV_ROW_LINES;
#endif
    m_dvListCtrlSessions =
        new wxDataViewTreeCtrl(GetSplitterPageLeft(), wxID_ANY,
                               wxDefaultPosition, wxDefaultSize, treeStyle);
    m_acceleratorInterceptor =
        std::make_unique<AcceleratorInterceptor>(m_dvListCtrlSessions);
    wxSize textSize = GetTextExtent("Tp");
    m_dvListCtrlSessions->SetRowHeight(textSize.GetHeight() +
                                       (2 * kLineHeightSpacer));
    auto col = m_dvListCtrlSessions->GetColumn(0);
    if (col) {
      col->GetRenderer()->SetMode(wxDATAVIEW_CELL_INERT);
    }
    m_leftPaneMainSizer->Add(m_dvListCtrlSessions,
                             wxSizerFlags(1).Expand().Border(wxALL, 5));
    GetSplitterPageLeft()->Layout();
    m_dvListCtrlSessions->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED,
                               &MainView::OnSelectionChanged, this);
    m_dvListCtrlSessions->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU,
                               &MainView::OnContextMenu, this);
  } else {
    m_sessionsBook->DeleteAllPages();
    m_history.Clear();
  }

  BuildGroups();
}

void MainView::BuildGroups() {
  // Group nodes are created on demand when sessions are added; nothing to
  // pre-populate here.
  m_dvListCtrlSessions->DeleteAllItems();
}

wxDataViewItem MainView::EnsureGroup(const wxString &groupName) {
  // Return existing node if already present.
  wxDataViewItem existing = GroupNode(groupName);
  if (existing.IsOk()) {
    return existing;
  }

  auto &bmps = AppManager::Get().GetBitmaps();
  auto *page = new GroupPage(m_sessionsBook);
  m_sessionsBook->AddPage(page, groupName);

  wxDataViewItem node = m_dvListCtrlSessions->AppendContainer(
      wxDataViewItem(nullptr), groupName, wxWithImages::NO_IMAGE,
      wxWithImages::NO_IMAGE,
      new GroupItemData(page, groupName == kTerminalsGroupName));
  m_dvListCtrlSessions->SetItemIcon(node, bmps.GetByAlias("folder", false));
  m_dvListCtrlSessions->SetItemExpandedIcon(
      node, bmps.GetByAlias("folder-open", false));
  return node;
}

wxDataViewItem MainView::GroupNode(const wxString &groupName) const {
  wxDataViewItem root(nullptr);
  const int count = m_dvListCtrlSessions->GetChildCount(root);
  for (int i = 0; i < count; ++i) {
    wxDataViewItem child = m_dvListCtrlSessions->GetNthChild(root, i);
    if (m_dvListCtrlSessions->GetItemText(child) == groupName) {
      return child;
    }
  }
  return wxDataViewItem(nullptr);
}

wxDataViewItem MainView::ItemFromName(const wxString &name) const {
  if (name.empty()) {
    return wxDataViewItem(nullptr);
  }
  for (const wxDataViewItem &leaf : SessionItemsInOrder()) {
    if (m_dvListCtrlSessions->GetItemText(leaf) == name) {
      return leaf;
    }
  }
  return wxDataViewItem(nullptr);
}

bool MainView::IsTerminalNode(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return false;
  }

  auto page = PageFromItem(item);
  return page->IsPlainTerminal();
}

GroupItemData *MainView::GetGroupItemData(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return nullptr;
  }
  auto *groupItemData =
      dynamic_cast<GroupItemData *>(m_dvListCtrlSessions->GetItemData(item));
  return groupItemData;
}

SessionPage *MainView::PageFromItem(const wxDataViewItem &item) const {
  if (!item.IsOk() || m_dvListCtrlSessions->IsContainer(item)) {
    return nullptr;
  }
  auto *data =
      dynamic_cast<SessionItemData *>(m_dvListCtrlSessions->GetItemData(item));
  return data ? data->page : nullptr;
}

wxDataViewItem MainView::AddSessionLeaf(const wxString &groupName,
                                        const wxString &name,
                                        SessionPage *page) {
  wxDataViewItem parent = EnsureGroup(groupName);
  if (!parent.IsOk()) {
    KLOG_ERROR() << "No agent group for '" << groupName
                 << "'; session leaf not added";
    return wxDataViewItem(nullptr);
  }
  wxDataViewItem item = m_dvListCtrlSessions->AppendItem(
      parent, name, wxWithImages::NO_IMAGE, new SessionItemData(page));
  SetAgentIcon(item);
  m_dvListCtrlSessions->Expand(parent);
  return item;
}

std::vector<wxDataViewItem> MainView::SessionItemsInOrder() const {
  std::vector<wxDataViewItem> leaves;
  wxDataViewItem root(nullptr);
  const int groupCount = m_dvListCtrlSessions->GetChildCount(root);
  for (int i = 0; i < groupCount; ++i) {
    wxDataViewItem group = m_dvListCtrlSessions->GetNthChild(root, i);
    const int childCount = m_dvListCtrlSessions->GetChildCount(group);
    for (int j = 0; j < childCount; ++j) {
      leaves.push_back(m_dvListCtrlSessions->GetNthChild(group, j));
    }
  }
  return leaves;
}

void MainView::SetAgentIcon(const wxDataViewItem &item) {
  SessionPage *page = PageFromItem(item);
  if (page == nullptr) {
    return;
  }
  if (page->IsPlainTerminal()) {
    const wxBitmapBundle &icon =
        AppManager::Get().GetBitmaps().GetByAlias("terminal", false);
    m_dvListCtrlSessions->SetItemIcon(item, icon);
  } else {
    const wxBitmapBundle &icon = AppManager::Get().GetBitmaps().GetByAlias(
        page->GetSession().agentName, false);
    m_dvListCtrlSessions->SetItemIcon(item, icon);
  }
}

MainView::~MainView() {
  Unbind(wxEVT_SESSION_IDLE, &MainView::OnSessionIdle, this);
  Unbind(wxEVT_SESSION_ACTIVE, &MainView::OnSessionActive, this);
  Unbind(wxEVT_SESSION_EXITED, &MainView::OnSessionExited, this);
  Unbind(wxEVT_IDLE, &MainView::OnIdleEvent, this);
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
  std::optional<AgentDef> agent{std::nullopt};
  if (!req.plainTerminal) {
    const AgentDef *pagent = m_registry->FindAgent(req.agentName);
    if (pagent == nullptr) {
      wxMessageBox(
          wxString::Format("Could not find agent '%s'.", req.agentName),
          "Launch failed", wxOK | wxICON_ERROR, this);
      return false;
    }
    agent = *pagent;
  }

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

  auto *page = new SessionPage(m_sessionsBook, agent, *session, req.resume);
  m_sessionsBook->AddPage(page, session->name, /*select=*/true);

  wxDataViewItem item = AddSessionLeaf(session->groupName, session->name, page);
  m_history.Push(Tab{
      .title = session.value().name,
      .agentName = session.value().agentName,
  });

  if (item.IsOk()) {
    m_dvListCtrlSessions->Select(item);
  }
  m_sessionsBook->SetSelection(m_sessionsBook->FindPage(page));

  if (req.plainTerminal) {
    KLOG_INFO() << "Launched terminal";
  } else {
    KLOG_INFO() << "Launched session '" << session.value().name << "' (agent "
                << req.agentName << ", cwd '" << req.workingDir << "')";
  }

  auto &prefs = AppManager::Get().GetPrefs();
  PushRecent(prefs.recentWorkingDirs, req.workingDir);
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not persist recent working dirs: " << st.message();
  }
  return true;
}

SessionPage *MainView::AddSessionPage(const Session &session, bool resume) {
  const AgentDef *agent = m_registry->FindAgent(session.agentName);
  if (agent == nullptr) {
    KLOG_WARN() << "Skipping restore: unknown agent '" << session.agentName
                << "'";
    return nullptr;
  }

  auto *page = new SessionPage(m_sessionsBook, *agent, session, resume);
  m_sessionsBook->AddPage(page, session.name, /*select=*/true);
  m_history.Push(Tab{
      .title = session.name,
      .agentName = session.agentName,
  });
  wxDataViewItem item = AddSessionLeaf(session.groupName, session.name, page);
  if (item.IsOk()) {
    m_dvListCtrlSessions->Select(item);
  }
  m_sessionsBook->SetSelection(m_sessionsBook->FindPage(page));
  return page;
}

void MainView::RestoreSessions() {
  const auto &sessions = m_workspace->Sessions();
  if (sessions.empty()) {
    return;
  }

  const auto &prefs = AppManager::Get().GetPrefs();
  const wxString wantSelected = prefs.lastSelectedSession;
  int restored = 0;

  for (const Session &s : sessions) {
    auto *page = AddSessionPage(s, /*resume=*/true);
    if (page) {
      page->GetTerminal()->EnsureStarted();
      ++restored;
    }
  }

  KLOG_INFO() << "Restored " << restored << " session(s)";

  wxDataViewItem item;
  if (!wantSelected.empty()) {
    item = ItemFromName(wantSelected);
  }
  if (!item.IsOk()) {
    std::vector<wxDataViewItem> leaves = SessionItemsInOrder();
    if (!leaves.empty()) {
      item = leaves.front();
    }
  }
  if (item.IsOk()) {
    m_dvListCtrlSessions->Select(item);
    DoSetSession(m_dvListCtrlSessions->GetItemText(item));
  }
}

void MainView::DoSetSession(const wxString &name) {
  wxDataViewItem item = ItemFromName(name);
  if (!item.IsOk()) {
    return;
  }
  SessionPage *session = PageFromItem(item);
  if (session == nullptr) {
    return;
  }
  m_sessionsBook->SetSelection(m_sessionsBook->FindPage(session));

  m_history.Push(Tab{
      .title = name,
      .agentName = wxEmptyString,
  });

  SetAgentIcon(item);
  session->GetTerminal()->CallAfter(&wxTerminalViewCtrl::SetFocus);
}

void MainView::OnSelectionChanged(wxDataViewEvent &event) {
  event.Skip();
  wxDataViewItem item = m_dvListCtrlSessions->GetSelection();
  CHECK_ITEM_RETURN(item);

  if (auto *data = GetGroupItemData(item); data != nullptr) {
    if (data->page) {
      m_sessionsBook->SetSelection(m_sessionsBook->FindPage(data->page));
    }
    wxTheApp->GetTopWindow()->SetLabel(_("Kennel"));
    return;
  }
  DoSetSession(m_dvListCtrlSessions->GetItemText(item));
}

void MainView::OnSessionExited(wxCommandEvent &e) {
  wxString name = e.GetString();
  DeleteByName(name);
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
        wxString::Format(_("Waiting for %d sessions..."), m_pendingIdle));
  }
}

void MainView::OnSessionActive(wxCommandEvent &e) { e.Skip(); }

void MainView::ApplyFont(const wxFont &f) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetFont(f);
  if (!active) {
    return;
  }
  for (size_t i = 0; i < m_sessionsBook->GetPageCount(); ++i) {
    SessionPage *sp = dynamic_cast<SessionPage *>(m_sessionsBook->GetPage(i));
    if (!sp || !sp->GetTerminal())
      continue;
    sp->ApplyTheme(*active);
    sp->GetTerminal()->SendSizeEvent();
  }
  KLOG_INFO() << "Applied terminal font '" << f.GetFaceName() << "' to "
              << static_cast<int>(SessionCount()) << " terminal(s)";
  SavePrefs();
  SendSizeEvent(); // Force the terminals to recalculate their size
}

void MainView::ApplyOptimizedDrawing() {
  bool optimized = AppManager::Get().GetPrefs().terminalOptimizedDrawing;
  for (size_t i = 0; i < m_sessionsBook->GetPageCount(); ++i) {
    SessionPage *sp = dynamic_cast<SessionPage *>(m_sessionsBook->GetPage(i));
    if (!sp || !sp->GetTerminal())
      continue;
    sp->GetTerminal()->EnableSafeDrawing(!optimized);
    sp->GetTerminal()->Refresh();
  }
}

void MainView::ApplyTheme(const wxString &themeName) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetTheme(themeName);
  if (!active) {
    return;
  }
  for (size_t i = 0; i < m_sessionsBook->GetPageCount(); ++i) {
    SessionPage *sp = dynamic_cast<SessionPage *>(m_sessionsBook->GetPage(i));
    if (!sp || !sp->GetTerminal())
      continue;
    sp->ApplyTheme(*active);
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

bool MainView::CanRefreshCurrent() const {
  return IsSelectionSessionGroup() || IsSelectionSession();
}

bool MainView::IsSelectionSessionGroup() const {
  auto item = m_dvListCtrlSessions->GetSelection();
  CHECK_ITEM_RETURN_FALSE(item);

  auto groupData = GetGroupItemData(item);
  return groupData && groupData->IsSessionGroup();
}

bool MainView::IsSelectionSession() const {
  auto item = m_dvListCtrlSessions->GetSelection();
  CHECK_ITEM_RETURN_FALSE(item);

  auto page = PageFromItem(item);
  return page && !page->IsPlainTerminal();
}

bool MainView::IsSelectionTerminal() const {
  auto item = m_dvListCtrlSessions->GetSelection();
  CHECK_ITEM_RETURN_FALSE(item);

  auto page = PageFromItem(item);
  return page && page->IsPlainTerminal();
}

bool MainView::IsSelectionTerminalGroup() const {
  auto item = m_dvListCtrlSessions->GetSelection();
  CHECK_ITEM_RETURN_FALSE(item);
  auto groupData = GetGroupItemData(item);
  return groupData && groupData->IsTerminalsGroup();
}

void MainView::RefreshCurrentSelection() {
  auto RefreshItem = [this](const wxDataViewItem &item) {
    SessionPage *page = PageFromItem(item);
    if (page) {
      page->CallAfter(&SessionPage::Restart);
      m_pendingIdle++;
    }
  };

  if (IsSelectionSessionGroup()) {
    auto item = m_dvListCtrlSessions->GetSelection();
    CHECK_ITEM_RETURN(item);

    auto count = m_dvListCtrlSessions->GetChildCount(item);
    for (int i = 0; i < count; ++i) {
      wxDataViewItem child = m_dvListCtrlSessions->GetNthChild(item, i);
      RefreshItem(child);
    }
  } else if (IsSelectionSession()) {
    RefreshItem(m_dvListCtrlSessions->GetSelection());
  }

  if (m_pendingIdle) {
    GetMainFrame()->SetActivityText(
        wxString::Format(_("Waiting for %d sessions..."), m_pendingIdle));
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
  BuildGroups();
  m_workspace->CloseAll();
  m_workspace->Persist();
  m_history.Clear();
}

void MainView::DeleteByName(const wxString &name) {
  wxDataViewItem item = ItemFromName(name);
  if (!item.IsOk()) {
    return;
  }
  SessionPage *page = PageFromItem(item);
  if (page == nullptr) {
    KLOG_ERROR() << "Session leaf '" << name << "' has no page!";
    return;
  }
  m_sessionsBook->DeletePage(m_sessionsBook->FindPage(page));

  // Remember the parent group before removing the leaf.
  wxDataViewItem parent = m_dvListCtrlSessions->GetItemParent(item);
  m_dvListCtrlSessions->DeleteItem(item);
  m_history.Pop(Tab{.title = name});
  m_workspace->Close(name);
  m_workspace->Persist();

  RemoveGroupIfEmpty(parent);

  std::vector<wxDataViewItem> leaves = SessionItemsInOrder();
  if (!leaves.empty()) {
    m_dvListCtrlSessions->Select(leaves.front());
    SessionPage *session = PageFromItem(leaves.front());
    if (session) {
      m_sessionsBook->SetSelection(m_sessionsBook->FindPage(session));
      session->GetTerminal()->CallAfter(&wxTerminalViewCtrl::SetFocus);
    }
  } else {
    wxTheApp->GetTopWindow()->SetLabel(_("Kennel"));
  }
}

SessionPage *MainView::GetSessionPage(const wxString &name) {
  return PageFromItem(ItemFromName(name));
}

void MainView::DoGroupMenu(const wxDataViewItem &item) {
  auto *data = GetGroupItemData(item);
  CHECK_NOT_NULL_RETURN(data);

  if (data->IsTerminalsGroup()) {
    wxMenu menu;
    menu.Append(wxID_ADD, _("New Terminal..."));
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { StartTerminal(); }, wxID_ADD);
    m_dvListCtrlSessions->PopupMenu(&menu);
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
    if (m_dvListCtrlSessions->GetItemText(item) == _("Default")) {
      menu.Enable(XRCID("rename-group"), false);
    }

    menu.Bind(
        wxEVT_MENU,
        [item, this](wxCommandEvent &) {
          StartAgent(wxEmptyString, m_dvListCtrlSessions->GetItemText(item));
        },
        wxID_ADD);

    menu.Bind(
        wxEVT_MENU, [item, this](wxCommandEvent &) { RenameGroup(item); },
        XRCID("rename-group"));

    menu.Bind(
        wxEVT_MENU,
        [item, this](wxCommandEvent &) {
          auto count = m_dvListCtrlSessions->GetChildCount(item);
          wxString msg;
          msg << _("This will close ") << count << _(" session(s).\nContinue?");
          if (wxMessageBox(msg, "Kennel",
                           wxICON_WARNING | wxYES_NO | wxCANCEL |
                               wxCANCEL_DEFAULT) != wxYES) {
            return;
          }
          for (int i = 0; i < count; ++i) {
            wxDataViewItem child = m_dvListCtrlSessions->GetNthChild(item, i);
            wxString name = m_dvListCtrlSessions->GetItemText(child);
            CallAfter(&MainView::DeleteByName, name);
          }
        },
        wxID_CLOSE_ALL);

    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { RefreshCurrentSelection(); },
        XRCID("refresh-sessions"));
    m_dvListCtrlSessions->PopupMenu(&menu);
  }
}

void MainView::Traverse(
    std::function<bool(const wxDataViewItem &)> visit) const {
  wxDataViewItem root{nullptr};
  const int groupCount = m_dvListCtrlSessions->GetChildCount(root);

  for (int i = 0; i < groupCount; ++i) {
    wxDataViewItem group = m_dvListCtrlSessions->GetNthChild(root, i);

    // Visit the group first
    if (!visit(group)) {
      return;
    }

    // Visit all children of the group
    const int childCount = m_dvListCtrlSessions->GetChildCount(group);
    for (int j = 0; j < childCount; ++j) {
      wxDataViewItem child = m_dvListCtrlSessions->GetNthChild(group, j);
      if (!visit(child)) {
        return;
      }
    }
  }
}

bool MainView::IsNameExist(const wxString &name) const {
  bool matchFound{false};
  auto checkIfNameExists = [&name, &matchFound,
                            this](const wxDataViewItem &item) {
    if (m_dvListCtrlSessions->GetItemText(item) == name) {
      matchFound = true;
      return false;
    }
    return true;
  };
  Traverse(checkIfNameExists);
  return matchFound;
}

wxString MainView::GetSelectedItemText() const {
  auto item = m_dvListCtrlSessions->GetSelection();
  if (!item.IsOk()) {
    return wxEmptyString;
  }
  return m_dvListCtrlSessions->GetItemText(item);
}

void MainView::RenameTerminal(const wxDataViewItem &item) {
  auto *page = PageFromItem(item);
  CHECK_NOT_NULL_RETURN(page);

  const wxString oldName = m_dvListCtrlSessions->GetItemText(item);
  wxString newName =
      ::wxGetTextFromUser(_("New Terminal Name:"), "Kennel", oldName);
  if (newName.empty() || newName == oldName)
    return;

  if (IsNameExist(newName)) {
    wxMessageBox(_("An item with this name already exist"), "Kennel",
                 wxICON_WARNING | wxOK | wxCENTER, wxTheApp->GetTopWindow());
    return;
  }
  m_dvListCtrlSessions->SetItemText(item, newName);
  page->SetDefaultSessionName(newName);
}

void MainView::RenameGroup(const wxDataViewItem &item) {
  if (!item.IsOk() || !m_dvListCtrlSessions->IsContainer(item)) {
    return;
  }

  const wxString oldName = m_dvListCtrlSessions->GetItemText(item);
  const wxString newName =
      ::wxGetTextFromUser(_("New Group Name:"), "Kennel", oldName);
  if (newName.empty() || newName == oldName) {
    return;
  }

  if (IsNameExist(newName)) {
    wxMessageBox(_("An item with this name already exist"), "Kennel",
                 wxICON_WARNING | wxOK | wxCENTER, wxTheApp->GetTopWindow());
    return;
  }

  if (Status st = m_workspace->RenameGroup(oldName, newName); !st.ok()) {
    wxMessageBox(st.message(), "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }

  // Retag the live session pages so their in-memory group matches.
  const int count = m_dvListCtrlSessions->GetChildCount(item);
  for (int i = 0; i < count; ++i) {
    SessionPage *page =
        PageFromItem(m_dvListCtrlSessions->GetNthChild(item, i));
    if (page) {
      page->GetSession().groupName = newName;
    }
  }

  // Update the tree node and its backing simplebook page label.
  m_dvListCtrlSessions->SetItemText(item, newName);
  if (auto *data = dynamic_cast<GroupItemData *>(
          m_dvListCtrlSessions->GetItemData(item));
      data && data->page) {
    const int pageIdx = m_sessionsBook->FindPage(data->page);
    if (pageIdx != wxNOT_FOUND) {
      m_sessionsBook->SetPageText(pageIdx, newName);
    }
  }

  m_workspace->Persist();
}

void MainView::RemoveGroupIfEmpty(const wxDataViewItem &group) {
  if (!group.IsOk() || m_dvListCtrlSessions->GetChildCount(group) != 0) {
    return;
  }
  // The "Default" group must always exist.
  if (m_dvListCtrlSessions->GetItemText(group) == _("Default")) {
    return;
  }
  auto *data =
      dynamic_cast<GroupItemData *>(m_dvListCtrlSessions->GetItemData(group));
  if (data && data->page) {
    m_sessionsBook->DeletePage(m_sessionsBook->FindPage(data->page));
  }
  m_dvListCtrlSessions->DeleteItem(group);
}

void MainView::MoveSessionToGroup(const wxDataViewItem &item,
                                  const wxString &targetGroup) {
  SessionPage *page = PageFromItem(item);
  if (page == nullptr) {
    return;
  }
  const wxString name = m_dvListCtrlSessions->GetItemText(item);

  if (Status st = m_workspace->MoveSession(name, targetGroup); !st.ok()) {
    wxMessageBox(st.message(), "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }
  page->GetSession().groupName = targetGroup;

  // Reparent in the tree: the leaf can't be moved in place, so drop it and
  // re-add it (with fresh client data) under the target group. The page stays
  // in the simplebook, so no terminal is disturbed.
  const wxDataViewItem sourceGroup = m_dvListCtrlSessions->GetItemParent(item);
  m_dvListCtrlSessions->DeleteItem(item);
  wxDataViewItem moved = AddSessionLeaf(targetGroup, name, page);
  RemoveGroupIfEmpty(sourceGroup);
  m_workspace->Persist();

  if (moved.IsOk()) {
    m_dvListCtrlSessions->Select(moved);
  }
}

void MainView::OnContextMenu(wxDataViewEvent &event) {
  auto item = event.GetItem();
  if (!item.IsOk()) {
    return;
  }
  if (m_dvListCtrlSessions->IsContainer(item)) {
    DoGroupMenu(item);
    return;
  }

  wxMenu menu;
  menu.Append(wxID_CLOSE, _("Close"));
  wxString name = m_dvListCtrlSessions->GetItemText(item);
  menu.Bind(
      wxEVT_MENU,
      [name, this](wxCommandEvent &) {
        wxString msg;
        msg << _("This will close session: ") << name << _("\nContinue?");
        if (wxMessageBox(msg, "Kennel",
                         wxICON_WARNING | wxYES_NO | wxCANCEL |
                             wxYES_DEFAULT) != wxYES) {
          return;
        }
        CallAfter(&MainView::DeleteByName, name);
      },
      wxID_CLOSE);

  if (IsTerminalNode(item)) {
    menu.Append(XRCID("rename-terminal"), _("Rename"));
    menu.Bind(
        wxEVT_MENU, [item, this](wxCommandEvent &) { RenameTerminal(item); },
        XRCID("rename-terminal"));
    m_dvListCtrlSessions->PopupMenu(&menu);
    return;
  }

  // "Move to Group" submenu: every existing group except the current one,
  // plus an option to create a new group.
  const wxDataViewItem parent = m_dvListCtrlSessions->GetItemParent(item);
  const wxString currentGroup =
      parent.IsOk() ? m_dvListCtrlSessions->GetItemText(parent) : wxString{};
  auto *moveMenu = new wxMenu;
  auto groups = AppManager::Get().Groups();
  for (const wxString &group : groups) {
    if (group == currentGroup) {
      continue;
    }
    wxMenuItem *mi = moveMenu->Append(wxID_ANY, group);
    menu.Bind(
        wxEVT_MENU,
        [item, group, this](wxCommandEvent &) {
          MoveSessionToGroup(item, group);
        },
        mi->GetId());
  }

  if (groups.size() > 1) { // exluding self
    moveMenu->AppendSeparator();
  }

  wxMenuItem *newGroupItem = moveMenu->Append(wxID_ANY, _("New Group..."));
  menu.Bind(
      wxEVT_MENU,
      [item, this](wxCommandEvent &) {
        const wxString group =
            ::wxGetTextFromUser(_("New Group Name:"), "Kennel");
        if (!group.empty()) {
          MoveSessionToGroup(item, group);
        }
      },
      newGroupItem->GetId());
  menu.AppendSeparator();
  menu.AppendSubMenu(moveMenu, _("Move to Group"));

  menu.AppendSeparator();
  menu.Append(wxID_REFRESH, _("Refresh"));
  menu.Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { RefreshCurrentSelection(); },
      wxID_REFRESH);

  m_dvListCtrlSessions->PopupMenu(&menu);
}

SessionPage *MainView::GetActiveTerminal() {
  if (m_sessionsBook->GetSelection() == wxNOT_FOUND) {
    return nullptr;
  }
  return dynamic_cast<SessionPage *>(
      m_sessionsBook->GetPage(m_sessionsBook->GetSelection()));
}

void MainView::SelectSession(const wxString &sessionName) {
  wxDataViewItem item = ItemFromName(sessionName);
  if (!item.IsOk()) {
    return;
  }
  m_dvListCtrlSessions->Select(item);
  DoSetSession(sessionName);
}

size_t MainView::SessionCount() const { return SessionItemsInOrder().size(); }

void MainView::SelectSession(bool forward) {
  std::vector<wxDataViewItem> leaves = SessionItemsInOrder();
  if (leaves.empty()) {
    return;
  }
  wxDataViewItem selection = m_dvListCtrlSessions->GetSelection();
  int cur = wxNOT_FOUND;
  for (int i = 0; i < static_cast<int>(leaves.size()); ++i) {
    if (leaves[i] == selection) {
      cur = i;
      break;
    }
  }
  if (cur == wxNOT_FOUND) {
    cur = 0;
  } else if (forward) {
    cur = (cur + 1) % static_cast<int>(leaves.size());
  } else {
    cur = (cur - 1 + static_cast<int>(leaves.size())) %
          static_cast<int>(leaves.size());
  }
  SelectSession(m_dvListCtrlSessions->GetItemText(leaves[cur]));
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

void MainView::OnIdleEvent(wxIdleEvent &e) {
  e.Skip();
  if (!m_idleHandled && GetActiveTerminal()) {
    m_idleHandled = true;
    GetActiveTerminal()->SetFocus();
  }
}
