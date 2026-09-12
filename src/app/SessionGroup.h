#pragma once

#include <wx/string.h>
#include <wx/translation.h>

class SessionPage;

// Data-only representation of a session group: a name, a "kind" flag, and
// its persisted icon. Holds no session list and no reference to the UI
// (tree item, notebook page, ...) — MainView owns the tree/notebook and is
// the only source of truth for which sessions belong to this group.
class SessionGroup {
public:
  SessionGroup(const wxString &groupName, bool terminalsGroup);

  inline const wxString &GetGroupName() const { return m_groupName; }
  inline void SetGroupName(const wxString &groupName) {
    m_groupName = groupName;
  }

  inline bool IsTerminalsGroup() const { return m_terminalsGroup; }
  inline bool IsSessionGroup() const { return !IsTerminalsGroup(); }
  inline bool IsDefaultGroup() const { return GetGroupName() == _("Default"); }

  // The most recently shown session in this group; used when the user
  // selects the group's tree node directly rather than a session leaf.
  inline SessionPage *GetLastActive() const { return m_lastActive; }
  inline void SetLastActive(SessionPage *page) { m_lastActive = page; }

  // Persisted icon alias for this group (e.g. "group-red"), assigned once
  // and kept for the group's lifetime.
  inline const wxString &GetIcon() const { return m_icon; }
  inline void SetIcon(const wxString &icon) { m_icon = icon; }

private:
  wxString m_groupName;
  bool m_terminalsGroup{false};
  SessionPage *m_lastActive{nullptr};
  wxString m_icon;
};
