#pragma once

#include "app/PageViewEvent.hpp"
#include "app/SessionGroup.h"

#include <wx/arrstr.h>
#include <wx/bmpbndl.h>
#include <wx/intl.h>

#include <memory>
#include <vector>

class SessionPage;
class FilePage;

// Names of the two built-in containers next to the user's own groups.
inline const wxString kTerminalsGroupName = _("Terminals");
inline const wxString kFilesGroupName = _("Files");

// The containers "Terminals" and "Files" are not groups: no session or group
// may use their names.
inline bool IsReservedGroupName(const wxString &name) {
  wxString trimmed = name;
  trimmed.Trim().Trim(false);
  return trimmed.IsSameAs(kTerminalsGroupName, false) ||
         trimmed.IsSameAs(kFilesGroupName, false);
}

// Icon aliases for freshly created groups; one is picked at random and
// persisted so the group keeps its color across restarts.
inline constexpr const char *kGroupIconAliases[] = {
    "group-red",  "group-orange", "group-lime",   "group-green",  "group-teal",
    "group-cyan", "group-blue",   "group-indigo", "group-purple", "group-pink",
};

// Which groups and pages exist: the groups (plus the "Terminals" and "Files"
// containers) and the pages in each of them, in the order they were added. It
// is the only source of truth for that; MainView owns the pages themselves
// (the windows) and tells this registry about them. The views (FlatView)
// show what it holds. It has no widgets, no selection and sends no events.
class PageRegistry {
public:
  PageRegistry() = default;
  PageRegistry(const PageRegistry &) = delete;
  PageRegistry &operator=(const PageRegistry &) = delete;

  // ---- Groups ----------------------------------------------------------
  // Returns the group, creating it first if needed.
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
  // Drops every group but "Default" that has no page left.
  void RemoveEmptyGroups();

  // The page to show when a group is chosen: the Files group's first file,
  // otherwise the group's last active (or first) session.
  wxWindow *GetGroupDefaultPage(const wxString &name) const;

  // ---- Pages -----------------------------------------------------------
  // Adds `page` to its group. False if the group already has a session with
  // that name.
  bool AddSession(SessionPage *page);
  void AddFile(FilePage *page);
  void RemovePage(wxWindow *page);
  // Moves a session to another group (created if needed) and updates the
  // session's group name.
  void MoveSession(SessionPage *page, const wxString &toGroup);
  // Sets what `page` shows as its status (the spinner, the check mark, the
  // hand); an icon that is not ok clears it.
  void SetPageStatusIcon(wxWindow *page, const wxBitmapBundle &icon);

  SessionPage *FindSession(const wxString &group, const wxString &name) const;
  // The session or file `ref` names, or nullptr.
  wxWindow *FindPage(const SessionRef &ref) const;
  FilePage *FindFile(const wxString &key) const;
  std::vector<SessionPage *> GetGroupSessions(const wxString &group) const;
  std::vector<SessionPage *> GetAllSessions() const;
  std::vector<FilePage *> GetFilePages() const;
  // Every page, in group order.
  std::vector<PageInfo> GetPages() const;
  // What to show after the active page was removed: a session of
  // `preferredGroup`, else any session, else any file.
  wxWindow *GetFallbackPage(const wxString &preferredGroup) const;

  void Clear();

private:
  struct Leaf {
    wxWindow *page{nullptr}; // A SessionPage or a FilePage; not owned
    wxBitmapBundle icon;
    wxBitmapBundle statusIcon;
  };
  struct Entry {
    std::unique_ptr<SessionGroup> group;
    std::vector<Leaf> leaves;
  };

  Entry *FindEntry(const wxString &name);
  const Entry *FindEntry(const wxString &name) const;
  // The group that holds `page`, or nullptr.
  Entry *FindEntryOf(wxWindow *page);
  void AddLeaf(const wxString &group, wxWindow *page,
               const wxBitmapBundle &icon);

  // Default first, then in the order the groups were created.
  std::vector<Entry> m_groups;
};
