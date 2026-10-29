#include "app/PageRegistry.hpp"

#include "app/AssetBootstrap.h"
#include "app/FilePage.hpp"
#include "app/SessionPage.hpp"
#include "core/AppManager.h"
#include "core/Logger.h"

#include <wx/settings.h>

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

// The alias of the icon `group` is shown with.
wxString GroupIconAlias(const SessionGroup &group) {
  if (group.IsTerminalsGroup()) {
    return "terminals";
  }
  if (group.IsFilesGroup()) {
    return "folder";
  }
  if (group.IsDefaultGroup()) {
    return "group-default";
  }
  return group.GetIcon();
}

wxString LabelOf(wxWindow *page) {
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    return session->GetSession().name;
  }
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    return file->GetDisplayName();
  }
  return wxString{};
}

// What names the page inside its group: the session name, or the file key.
wxString KeyOf(wxWindow *page) {
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    return session->GetSession().name;
  }
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    return file->GetKey();
  }
  return wxString{};
}
} // namespace

// ---------------------------------------------------------------------------
// Lookup helpers
// ---------------------------------------------------------------------------

PageRegistry::Entry *PageRegistry::FindEntry(const wxString &name) {
  for (auto &entry : m_groups) {
    if (entry.group->GetGroupName() == name) {
      return &entry;
    }
  }
  return nullptr;
}

const PageRegistry::Entry *PageRegistry::FindEntry(const wxString &name) const {
  return const_cast<PageRegistry *>(this)->FindEntry(name);
}

PageRegistry::Entry *PageRegistry::FindEntryOf(wxWindow *page) {
  for (auto &entry : m_groups) {
    for (const auto &leaf : entry.leaves) {
      if (leaf.page == page) {
        return &entry;
      }
    }
  }
  return nullptr;
}

void PageRegistry::AddLeaf(const wxString &group, wxWindow *page,
                           const wxBitmapBundle &icon) {
  if (auto *entry = FindEntry(group)) {
    entry->leaves.push_back(Leaf{page, icon});
  }
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

SessionGroup *PageRegistry::EnsureGroup(const wxString &name) {
  if (auto *existing = GetGroup(name)) {
    return existing;
  }

  auto group = std::make_unique<SessionGroup>(name, name == kTerminalsGroupName,
                                              name == kFilesGroupName);
  if (group->IsSessionGroup() && !group->IsDefaultGroup()) {
    wxString iconAlias = LookupPersistedGroupIcon(name);
    if (iconAlias.empty()) {
      iconAlias = PickRandomGroupIcon();
    }
    group->SetIcon(iconAlias);
  }

  auto *result = group.get();
  Entry entry{std::move(group), {}};
  if (result->IsDefaultGroup()) {
    m_groups.insert(m_groups.begin(), std::move(entry));
  } else {
    m_groups.push_back(std::move(entry));
  }
  return result;
}

SessionGroup *PageRegistry::GetGroup(const wxString &name) const {
  const auto *entry = FindEntry(name);
  return entry ? entry->group.get() : nullptr;
}

std::vector<SessionGroup *> PageRegistry::GetGroups() const {
  std::vector<SessionGroup *> result;
  for (const auto &entry : m_groups) {
    result.push_back(entry.group.get());
  }
  return result;
}

wxArrayString PageRegistry::GetSessionGroupNames() const {
  wxArrayString names;
  for (const auto &entry : m_groups) {
    if (entry.group->IsSessionGroup()) {
      names.Add(entry.group->GetGroupName());
    }
  }
  return names;
}

wxString PageRegistry::GetFirstGroupName() const {
  return m_groups.empty() ? wxString{} : m_groups.front().group->GetGroupName();
}

size_t PageRegistry::GroupCount() const { return m_groups.size(); }

void PageRegistry::RenameGroup(SessionGroup *group, const wxString &newName) {
  CHECK_NOT_NULL_RETURN(group);
  const wxString oldName = group->GetGroupName();
  for (auto *page : GetGroupSessions(oldName)) {
    page->GetSession().groupName = newName;
  }
  group->SetGroupName(newName);
}

void PageRegistry::RemoveGroup(const wxString &name) {
  std::erase_if(m_groups, [&name](const Entry &entry) {
    return entry.group->GetGroupName() == name;
  });
}

void PageRegistry::RemoveEmptyGroups() {
  std::erase_if(m_groups, [](const Entry &entry) {
    return !entry.group->IsDefaultGroup() && entry.leaves.empty();
  });
}

wxWindow *PageRegistry::GetGroupDefaultPage(const wxString &name) const {
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

bool PageRegistry::AddSession(SessionPage *page) {
  const Session &session = page->GetSession();
  if (EnsureGroup(session.groupName) == nullptr) {
    KLOG_ERROR() << "No agent group for '" << session.groupName
                 << "'; session not added";
    return false;
  }
  if (FindSession(session.groupName, session.name) != nullptr) {
    KLOG_WARN() << "A session named '" << session.name
                << "' already exists in group '" << session.groupName << "'";
    return false;
  }
  AddLeaf(session.groupName, page, SessionIconFor(session));
  return true;
}

void PageRegistry::AddFile(FilePage *page) {
  EnsureGroup(kFilesGroupName);
  AddLeaf(kFilesGroupName, page,
          AppManager::Get().GetBitmaps().GetFileIcon(
              page->GetPath(), wxSystemSettings::GetAppearance().IsDark()));
}

void PageRegistry::RemovePage(wxWindow *page) {
  if (auto *entry = FindEntryOf(page)) {
    std::erase_if(entry->leaves,
                  [page](const Leaf &leaf) { return leaf.page == page; });
  }
}

void PageRegistry::MoveSession(SessionPage *page, const wxString &toGroup) {
  RemovePage(page);
  EnsureGroup(toGroup);
  page->GetSession().groupName = toGroup;
  AddLeaf(toGroup, page, SessionIconFor(page->GetSession()));
}

void PageRegistry::SetPageStatusIcon(wxWindow *page,
                                     const wxBitmapBundle &icon) {
  if (auto *entry = FindEntryOf(page)) {
    for (auto &leaf : entry->leaves) {
      if (leaf.page == page) {
        leaf.statusIcon = icon;
      }
    }
  }
}

SessionPage *PageRegistry::FindSession(const wxString &group,
                                       const wxString &name) const {
  for (auto *page : GetGroupSessions(group)) {
    if (page->GetSession().name == name) {
      return page;
    }
  }
  return nullptr;
}

wxWindow *PageRegistry::FindPage(const SessionRef &ref) const {
  if (ref.groupName == kFilesGroupName) {
    return FindFile(ref.name);
  }
  return FindSession(ref.groupName, ref.name);
}

FilePage *PageRegistry::FindFile(const wxString &key) const {
  for (auto *page : GetFilePages()) {
    if (page->GetKey() == key) {
      return page;
    }
  }
  return nullptr;
}

std::vector<SessionPage *>
PageRegistry::GetGroupSessions(const wxString &group) const {
  std::vector<SessionPage *> result;
  if (const auto *entry = FindEntry(group)) {
    for (const auto &leaf : entry->leaves) {
      if (auto *session = dynamic_cast<SessionPage *>(leaf.page)) {
        result.push_back(session);
      }
    }
  }
  return result;
}

std::vector<SessionPage *> PageRegistry::GetAllSessions() const {
  std::vector<SessionPage *> result;
  for (const auto &entry : m_groups) {
    for (const auto &leaf : entry.leaves) {
      if (auto *session = dynamic_cast<SessionPage *>(leaf.page)) {
        result.push_back(session);
      }
    }
  }
  return result;
}

std::vector<FilePage *> PageRegistry::GetFilePages() const {
  std::vector<FilePage *> result;
  if (const auto *entry = FindEntry(kFilesGroupName)) {
    for (const auto &leaf : entry->leaves) {
      if (auto *file = dynamic_cast<FilePage *>(leaf.page)) {
        result.push_back(file);
      }
    }
  }
  return result;
}

std::vector<PageInfo> PageRegistry::GetPages() const {
  std::vector<PageInfo> result;
  const auto &bitmaps = AppManager::Get().GetBitmaps();
  for (const auto &entry : m_groups) {
    const wxString groupName = entry.group->GetGroupName();
    const wxBitmapBundle groupIcon =
        bitmaps.GetByAlias(GroupIconAlias(*entry.group), false);
    for (const auto &leaf : entry.leaves) {
      wxString dir;
      Account account;
      if (auto *session = dynamic_cast<SessionPage *>(leaf.page)) {
        dir = session->GetCurrentDir();
      } else if (auto *file = dynamic_cast<FilePage *>(leaf.page)) {
        dir = file->GetFolder();
        account = file->GetAccount();
      }
      result.push_back({LabelOf(leaf.page), groupName, KeyOf(leaf.page), dir,
                        leaf.icon, groupIcon, leaf.statusIcon, account});
    }
  }
  return result;
}

wxWindow *PageRegistry::GetFallbackPage(const wxString &preferredGroup) const {
  if (!preferredGroup.empty()) {
    auto sessions = GetGroupSessions(preferredGroup);
    if (!sessions.empty()) {
      return sessions.front();
    }
  }
  for (const auto &entry : m_groups) {
    auto sessions = GetGroupSessions(entry.group->GetGroupName());
    if (!sessions.empty()) {
      return sessions.front();
    }
  }
  auto files = GetFilePages();
  return files.empty() ? nullptr : files.front();
}

void PageRegistry::Clear() { m_groups.clear(); }
