#pragma once

#include "app/PageViewEvent.hpp"
#include "app/SessionGroup.h"

#include <wx/arrstr.h>
#include <wx/clntdata.h>
#include <wx/dataview.h>
#include <wx/intl.h>
#include <wx/panel.h>

#include <memory>
#include <optional>
#include <vector>

class SessionPage;
class FilePage;

// Names of the two built-in containers next to the user's own groups.
inline const wxString kTerminalsGroupName = _("Terminals");
inline const wxString kFilesGroupName = _("Files");

// Icon aliases for freshly created groups; one is picked at random and
// persisted so the group keeps its color across restarts.
inline constexpr const char *kGroupIconAliases[] = {
    "group-red",  "group-orange", "group-lime",   "group-green",  "group-teal",
    "group-cyan", "group-blue",   "group-indigo", "group-purple", "group-pink",
};

// Client data on each group's container tree item. Owns the SessionGroup:
// deleting the tree item (via wxDataViewTreeStore) deletes this, which
// deletes the group.
class GroupItemData : public wxClientData {
public:
  explicit GroupItemData(std::unique_ptr<SessionGroup> g)
      : group{std::move(g)} {}
  std::unique_ptr<SessionGroup> group;
};

// Client data on each session leaf tree item. Non-owning: the SessionPage
// window is owned by MainView's book.
class SessionItemData : public wxClientData {
public:
  explicit SessionItemData(SessionPage *p) : page{p} {}
  SessionPage *page{nullptr};
};

// Client data on each file leaf under the "Files" container. Non-owning: the
// FilePage window is owned by MainView's book.
class FileItemData : public wxClientData {
public:
  explicit FileItemData(FilePage *p) : page{p} {}
  FilePage *page{nullptr};
};

// The Groups -> Sessions tree (plus the "Terminals" and "Files" containers).
// It is the only source of truth for which groups and pages exist; MainView
// owns the pages themselves and tells this view about them.
//
// Sends wxEVT_PAGEVIEW_SELECTED (through EventNotifier) when the user clicks a
// page, and wxEVT_PAGEVIEW_MENU to its parent chain when the user asks for a
// context menu. Follows the selection made anywhere else. Programmatic changes
// (SelectPage() and friends) never send events. It keeps no selection state:
// the selected page is the tree's selection.
class TreeView : public wxPanel {
public:
  explicit TreeView(wxWindow *parent);
  ~TreeView() override;

  // ---- Groups ----------------------------------------------------------
  // Returns the group, creating its container first if needed.
  SessionGroup *EnsureGroup(const wxString &name);
  SessionGroup *GetGroup(const wxString &name) const;
  std::vector<SessionGroup *> GetGroups() const;
  // Names of the groups that hold agent sessions (no Terminals / Files).
  wxArrayString GetSessionGroupNames() const;
  wxString GetFirstGroupName() const;
  size_t GroupCount() const;

  // Renames `group` and every session in it. The caller checks for clashes.
  void RenameGroup(SessionGroup *group, const wxString &newName);
  void RemoveGroup(const wxString &name);
  // Drops every container but "Default" that has no page left.
  void RemoveEmptyGroups();

  // The group of the selected item: the group itself for a group node, the
  // parent group for a session. nullptr for a file or no selection.
  SessionGroup *GetSelectedGroup() const;
  // The selected item if (and only if) it is a group node.
  SessionGroup *GetSelectedGroupNode() const;
  bool SelectGroup(const wxString &name);
  // The page to show when a group node is chosen: the Files group's first
  // file, otherwise the group's last active (or first) session.
  wxWindow *GetGroupDefaultPage(const wxString &name) const;

  // ---- Pages -----------------------------------------------------------
  // Adds a leaf for `page` under its group. False if the group already has a
  // session with that name.
  bool AddSession(SessionPage *page);
  void AddFile(FilePage *page);
  void RemovePage(wxWindow *page);
  // Moves a session's leaf to another group (created if needed) and updates
  // the session's group name.
  void MoveSession(SessionPage *page, const wxString &toGroup);
  // Re-reads `page`'s name into its leaf (after a rename).
  void UpdateLabel(SessionPage *page);

  SessionPage *FindSession(const wxString &group, const wxString &name) const;
  // The session or file `ref` names, or nullptr.
  wxWindow *FindPage(const SessionRef &ref) const;
  FilePage *FindFile(const wxString &key) const;
  std::vector<SessionPage *> GetGroupSessions(const wxString &group) const;
  std::vector<SessionPage *> GetAllSessions() const;
  std::vector<FilePage *> GetFilePages() const;
  // Every page, in tree order.
  std::vector<PageInfo> GetPages() const;
  // What to show after the active page was removed: a session of
  // `preferredGroup`, else any session, else any file.
  wxWindow *GetFallbackPage(const wxString &preferredGroup) const;

  // Selects the leaf of `ref`. Never sends events.
  void SelectPage(const SessionRef &ref);
  // The page of the selected leaf, or nullptr.
  wxWindow *GetSelectedPage() const;

  void Clear();

private:
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnContextMenu(wxDataViewEvent &event);
  void OnPageSelected(PageViewEvent &event);
  // `page` set: a menu for that page. Otherwise a menu for `groupName`, or
  // for empty space if that is empty too.
  void SendMenuEvent(const std::optional<SessionRef> &page,
                     const wxString &groupName);

  GroupItemData *GetGroupData(const wxDataViewItem &item) const;
  SessionItemData *GetSessionData(const wxDataViewItem &item) const;
  FileItemData *GetFileData(const wxDataViewItem &item) const;
  wxWindow *PageOf(const wxDataViewItem &item) const;
  // The name of the page of a leaf; nullopt for a group or no item.
  std::optional<SessionRef> RefOf(const wxDataViewItem &item) const;

  std::vector<wxDataViewItem> Children(const wxDataViewItem &parent) const;
  // The leaves of the group called `group` (none if there is no such group).
  std::vector<wxDataViewItem> LeavesOf(const wxString &group) const;
  wxDataViewItem FindGroupItem(const wxString &name) const;
  wxDataViewItem FindPageItem(wxWindow *page) const;
  // Appends a leaf for `data`'s page under `group` and gives it `icon`.
  void AppendLeaf(const wxDataViewItem &group, const wxString &label,
                  wxClientData *data, const wxBitmapBundle &icon);

  wxDataViewTreeCtrl *m_tree{nullptr};
};
