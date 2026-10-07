#include "app/TreeView.hpp"

#include "MainFrame.h"
#include "app/AssetBootstrap.h"
#include "app/FilePage.hpp"
#include "app/SessionPage.hpp"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Helpers.h"
#include "core/Logger.h"
#include <wx/filename.h>
#include <wx/sizer.h>

#include <algorithm>
#include <random>

namespace {
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

// Icon alias this group was assigned the last time workspace.json was
// written, or empty if `groupName` wasn't a persisted group at startup.
wxString LookupPersistedGroupIcon(const wxString &groupName) {
  for (const GroupMeta &g : AppManager::Get().InitialWorkspace().groups) {
    if (g.name == groupName) {
      return g.icon;
    }
  }
  return wxEmptyString;
}

// Icon shown on a session leaf: the agent's icon, or none for a plain
// terminal / an agent with no resolvable icon.
wxBitmapBundle SessionIconFor(const Session &session) {
  if (session.plainTerminal) {
    return wxBitmapBundle{};
  }
  const auto *agentDef =
      AppManager::Get().Adapters().FindAgent(session.agentName);
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

TreeView::TreeView(wxWindow *parent) : wxPanel(parent) {
  m_tree =
      new wxDataViewTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxDV_NO_HEADER | wxDV_ROW_LINES | wxDV_SINGLE);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_tree, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  StylePageView(m_tree);

  // Renaming is only offered via the context menu / F2 (MainView::RenameItem),
  // which goes through a proper dialog with validation. Make the tree's
  // auto-created text column inert so it can't fall into in-place label
  // editing (F2/slow double-click) as a second, unvalidated path.
  if (wxDataViewColumn *column = m_tree->GetColumn(0)) {
    column->GetRenderer()->SetMode(wxDATAVIEW_CELL_INERT);
  }

  m_tree->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &TreeView::OnSelectionChanged,
               this);
  m_tree->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &TreeView::OnContextMenu,
               this);
}

// ---------------------------------------------------------------------------
// Item helpers
// ---------------------------------------------------------------------------

GroupItemData *TreeView::GetGroupData(const wxDataViewItem &item) const {
  return item.IsOk() ? dynamic_cast<GroupItemData *>(m_tree->GetItemData(item))
                     : nullptr;
}

SessionItemData *TreeView::GetSessionData(const wxDataViewItem &item) const {
  return item.IsOk()
             ? dynamic_cast<SessionItemData *>(m_tree->GetItemData(item))
             : nullptr;
}

FileItemData *TreeView::GetFileData(const wxDataViewItem &item) const {
  return item.IsOk() ? dynamic_cast<FileItemData *>(m_tree->GetItemData(item))
                     : nullptr;
}

wxWindow *TreeView::PageOf(const wxDataViewItem &item) const {
  if (auto *session = GetSessionData(item)) {
    return session->page;
  }
  if (auto *file = GetFileData(item)) {
    return file->page;
  }
  return nullptr;
}

std::vector<wxDataViewItem>
TreeView::Children(const wxDataViewItem &parent) const {
  std::vector<wxDataViewItem> result;
  const int count = m_tree->GetChildCount(parent);
  for (int i = 0; i < count; ++i) {
    result.push_back(m_tree->GetNthChild(parent, i));
  }
  return result;
}

std::vector<wxDataViewItem> TreeView::LeavesOf(const wxString &group) const {
  // An invalid item stands for the root, so don't ask for its children.
  auto container = FindGroupItem(group);
  return container.IsOk() ? Children(container) : std::vector<wxDataViewItem>{};
}

wxDataViewItem TreeView::FindGroupItem(const wxString &name) const {
  for (const auto &item : Children(wxDataViewItem{})) {
    auto *data = GetGroupData(item);
    if (data && data->group->GetGroupName() == name) {
      return item;
    }
  }
  return wxDataViewItem{};
}

wxDataViewItem TreeView::FindPageItem(wxWindow *page) const {
  for (const auto &group : Children(wxDataViewItem{})) {
    for (const auto &leaf : Children(group)) {
      if (PageOf(leaf) == page) {
        return leaf;
      }
    }
  }
  return wxDataViewItem{};
}

void TreeView::AppendLeaf(const wxDataViewItem &group, const wxString &label,
                          wxClientData *data, const wxBitmapBundle &icon) {
  auto leaf =
      m_tree->AppendItem(group, label, wxDataViewTreeCtrl::NO_IMAGE, data);
  if (icon.IsOk()) {
    m_tree->SetItemIcon(leaf, icon);
  }
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

SessionGroup *TreeView::EnsureGroup(const wxString &name) {
  if (auto *existing = GetGroup(name)) {
    return existing;
  }

  auto ownedGroup = std::make_unique<SessionGroup>(
      name, name == kTerminalsGroupName, name == kFilesGroupName);
  auto *group = ownedGroup.get();

  wxString iconAlias;
  if (group->IsTerminalsGroup()) {
    iconAlias = "terminal";
  } else if (group->IsFilesGroup()) {
    iconAlias = "folder";
  } else if (group->IsDefaultGroup()) {
    iconAlias = "group-default";
  } else {
    iconAlias = LookupPersistedGroupIcon(name);
    if (iconAlias.empty()) {
      iconAlias = PickRandomGroupIcon();
    }
    group->SetIcon(iconAlias);
  }

  auto *itemData = new GroupItemData(std::move(ownedGroup));
  wxDataViewItem container;
  if (group->IsDefaultGroup()) {
    container = m_tree->PrependContainer(
        wxDataViewItem(), name, wxDataViewTreeCtrl::NO_IMAGE,
        wxDataViewTreeCtrl::NO_IMAGE, itemData);
  } else {
    container = m_tree->AppendContainer(wxDataViewItem(), name,
                                        wxDataViewTreeCtrl::NO_IMAGE,
                                        wxDataViewTreeCtrl::NO_IMAGE, itemData);
  }

  auto &bmps = AppManager::Get().GetBitmaps();
  m_tree->SetItemIcon(container, bmps.GetByAlias(iconAlias, false));
  m_tree->Expand(container);
  return group;
}

SessionGroup *TreeView::GetGroup(const wxString &name) const {
  auto *data = GetGroupData(FindGroupItem(name));
  return data ? data->group.get() : nullptr;
}

std::vector<SessionGroup *> TreeView::GetGroups() const {
  std::vector<SessionGroup *> result;
  for (const auto &item : Children(wxDataViewItem{})) {
    if (auto *data = GetGroupData(item)) {
      result.push_back(data->group.get());
    }
  }
  return result;
}

wxArrayString TreeView::GetSessionGroupNames() const {
  wxArrayString names;
  for (auto *group : GetGroups()) {
    if (group->IsSessionGroup()) {
      names.Add(group->GetGroupName());
    }
  }
  return names;
}

wxString TreeView::GetFirstGroupName() const {
  auto groups = GetGroups();
  return groups.empty() ? wxString{} : groups.front()->GetGroupName();
}

size_t TreeView::GroupCount() const {
  return static_cast<size_t>(m_tree->GetChildCount(wxDataViewItem()));
}

void TreeView::RenameGroup(SessionGroup *group, const wxString &newName) {
  CHECK_NOT_NULL_RETURN(group);
  const wxString oldName = group->GetGroupName();
  auto container = FindGroupItem(oldName);
  for (auto *page : GetGroupSessions(oldName)) {
    page->GetSession().groupName = newName;
  }
  group->SetGroupName(newName);
  if (container.IsOk()) {
    m_tree->SetItemText(container, newName);
  }
}

void TreeView::RemoveGroup(const wxString &name) {
  auto container = FindGroupItem(name);
  if (container.IsOk()) {
    m_tree->DeleteItem(container); // deletes GroupItemData -> group
  }
}

void TreeView::RemoveEmptyGroups() {
  std::vector<wxDataViewItem> empty;
  for (const auto &item : Children(wxDataViewItem{})) {
    auto *data = GetGroupData(item);
    if (data && !data->group->IsDefaultGroup() &&
        m_tree->GetChildCount(item) == 0) {
      empty.push_back(item);
    }
  }
  for (const auto &item : empty) {
    m_tree->DeleteItem(item);
  }
}

SessionGroup *TreeView::GetSelectedGroupNode() const {
  auto *data = GetGroupData(m_tree->GetSelection());
  return data ? data->group.get() : nullptr;
}

SessionGroup *TreeView::GetSelectedGroup() const {
  const auto item = m_tree->GetSelection();
  if (!item.IsOk()) {
    return nullptr;
  }
  if (auto *group = GetSelectedGroupNode()) {
    return group;
  }
  if (GetSessionData(item)) {
    auto *parent = GetGroupData(m_tree->GetItemParent(item));
    return parent ? parent->group.get() : nullptr;
  }
  return nullptr;
}

bool TreeView::SelectGroup(const wxString &name) {
  auto item = FindGroupItem(name);
  CHECK_ITEM_RETURN_FALSE(item);
  m_tree->Select(item);
  return true;
}

wxWindow *TreeView::GetGroupDefaultPage(const wxString &name) const {
  auto *group = GetGroup(name);
  if (group == nullptr) {
    return nullptr;
  }

  if (group->IsFilesGroup()) {
    auto files = GetFilePages();
    return files.empty() ? nullptr : files.front();
  }

  auto sessions = GetGroupSessions(name);
  if (sessions.empty()) {
    return nullptr;
  }
  if (const auto &lastActive = group->GetLastActive(); !lastActive.empty()) {
    if (auto *session = FindSession(name, lastActive)) {
      return session;
    }
  }
  return sessions.front();
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

bool TreeView::AddSession(SessionPage *page) {
  const Session &session = page->GetSession();
  if (EnsureGroup(session.groupName) == nullptr) {
    KLOG_ERROR() << "No agent group for '" << session.groupName
                 << "'; session leaf not added";
    return false;
  }
  if (FindSession(session.groupName, session.name) != nullptr) {
    KLOG_WARN() << "A session named '" << session.name
                << "' already exists in group '" << session.groupName << "'";
    return false;
  }
  AppendLeaf(FindGroupItem(session.groupName), session.name,
             new SessionItemData(page), SessionIconFor(session));
  return true;
}

void TreeView::AddFile(FilePage *page) {
  EnsureGroup(kFilesGroupName);
  AppendLeaf(FindGroupItem(kFilesGroupName), page->GetDisplayName(),
             new FileItemData(page),
             AppManager::Get().GetBitmaps().GetByAlias("file", false));
}

void TreeView::RemovePage(wxWindow *page) {
  auto leaf = FindPageItem(page);
  if (leaf.IsOk()) {
    m_tree->DeleteItem(leaf);
  }
}

void TreeView::MoveSession(SessionPage *page, const wxString &toGroup) {
  auto leaf = FindPageItem(page);
  if (leaf.IsOk()) {
    m_tree->DeleteItem(leaf);
  }
  EnsureGroup(toGroup);
  page->GetSession().groupName = toGroup;
  AppendLeaf(FindGroupItem(toGroup), page->GetSession().name,
             new SessionItemData(page), SessionIconFor(page->GetSession()));
}

void TreeView::UpdateLabel(SessionPage *page) {
  auto leaf = FindPageItem(page);
  if (leaf.IsOk()) {
    m_tree->SetItemText(leaf, page->GetSession().name);
  }
}

SessionPage *TreeView::FindSession(const wxString &group,
                                   const wxString &name) const {
  for (const auto &leaf : LeavesOf(group)) {
    auto *data = GetSessionData(leaf);
    if (data && data->page->GetSession().name == name) {
      return data->page;
    }
  }
  return nullptr;
}

FilePage *TreeView::FindFile(const wxString &key) const {
  for (auto *page : GetFilePages()) {
    if (page->GetKey() == key) {
      return page;
    }
  }
  return nullptr;
}

std::vector<SessionPage *>
TreeView::GetGroupSessions(const wxString &group) const {
  std::vector<SessionPage *> result;
  for (const auto &leaf : LeavesOf(group)) {
    if (auto *data = GetSessionData(leaf)) {
      result.push_back(data->page);
    }
  }
  return result;
}

std::vector<SessionPage *> TreeView::GetAllSessions() const {
  std::vector<SessionPage *> result;
  for (auto *group : GetGroups()) {
    auto sessions = GetGroupSessions(group->GetGroupName());
    result.insert(result.end(), sessions.begin(), sessions.end());
  }
  return result;
}

std::vector<FilePage *> TreeView::GetFilePages() const {
  std::vector<FilePage *> result;
  for (const auto &leaf : LeavesOf(kFilesGroupName)) {
    if (auto *data = GetFileData(leaf)) {
      result.push_back(data->page);
    }
  }
  return result;
}

std::vector<PageInfo> TreeView::GetPages() const {
  std::vector<PageInfo> result;
  for (const auto &group : Children(wxDataViewItem{})) {
    const wxString groupName = m_tree->GetItemText(group);
    for (const auto &leaf : Children(group)) {
      if (auto *page = PageOf(leaf)) {
        result.push_back({page, m_tree->GetItemText(leaf), groupName,
                          m_tree->GetItemIcon(leaf)});
      }
    }
  }
  return result;
}

wxWindow *TreeView::GetFallbackPage(const wxString &preferredGroup) const {
  if (!preferredGroup.empty()) {
    auto sessions = GetGroupSessions(preferredGroup);
    if (!sessions.empty()) {
      return sessions.front();
    }
  }
  for (auto *group : GetGroups()) {
    auto sessions = GetGroupSessions(group->GetGroupName());
    if (!sessions.empty()) {
      return sessions.front();
    }
  }
  auto files = GetFilePages();
  return files.empty() ? nullptr : files.front();
}

void TreeView::SelectPage(wxWindow *page) {
  CHECK_NOT_NULL_RETURN(page);
  auto leaf = FindPageItem(page);
  if (leaf.IsOk()) {
    m_tree->Select(leaf);
  }
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    if (auto *group = GetGroup(session->GetSession().groupName)) {
      group->SetLastActive(session->GetSession().name);
    }
  }
}

wxWindow *TreeView::GetSelectedPage() const {
  return PageOf(m_tree->GetSelection());
}

void TreeView::Clear() { m_tree->DeleteAllItems(); }

// ---------------------------------------------------------------------------
// User interaction
// ---------------------------------------------------------------------------

void TreeView::OnSelectionChanged(wxDataViewEvent &event) {
  auto item = event.GetItem();
  CHECK_ITEM_RETURN(item);

  if (auto *sessionData = GetSessionData(item)) {
    PageViewEvent evtSelected(wxEVT_PAGEVIEW_SELECTED);
    evtSelected.SetEventObject(this);
    evtSelected.SetUpdateRecent(true);
    evtSelected.SetGroupName(sessionData->page->GetSession().groupName);
    evtSelected.SetSessionName(sessionData->page->GetSession().name);
    EventNotifier::Get()->AddPendingEvent(evtSelected);
    return;
  }

  // Clicking a group node only toggles its expand/collapse state; it must
  // not change which page is selected/shown. Defer the toggle: calling
  // Collapse()/Expand() synchronously from within the selection-changed
  // handler mutates the tree while wxDataViewCtrl's generic (Windows)
  // implementation is still processing the click that triggered this
  // event, which crashes the app.
  const bool expanded = m_tree->IsExpanded(item);
  CallAfter([this, item, expanded] {
    if (expanded) {
      m_tree->Collapse(item);
    } else {
      m_tree->Expand(item);
    }
  });
}

void TreeView::OnContextMenu(wxDataViewEvent &event) {
  auto item = event.GetItem();
  if (!item.IsOk()) {
    SendMenuEvent(nullptr, wxEmptyString);
    return;
  }
  if (auto *group = GetGroupData(item)) {
    SendMenuEvent(nullptr, group->group->GetGroupName());
    return;
  }
  SendMenuEvent(PageOf(item), wxEmptyString);
}

void TreeView::SendMenuEvent(wxWindow *page, const wxString &groupName) {
  PageViewEvent menu(wxEVT_PAGEVIEW_MENU);
  menu.SetEventObject(this);
  menu.SetGroupName(groupName);
  ProcessWindowEvent(menu);
}
