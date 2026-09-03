#pragma once

#include "app/SessionPage.hpp"

#include <functional>
#include <vector>
#include <wx/dataview.h>
#include <wx/string.h>

// Data-only representation of a session group: a name, a "kind" flag, and
// the list of sessions that currently belong to it. Owns no window; MainView
// owns the tree item and the SessionPage windows.
class SessionGroup {
public:
  SessionGroup(const wxString &groupName, bool terminalsGroup);

  inline const wxString &GetGroupName() const { return m_groupName; }

  /**
   * Renames the group and updates every session's stored group name.
   * Persists the rename to the workspace.
   */
  void SetGroupName(const wxString &groupName);

  /**
   * Adds an existing session page to the group.
   *
   * @param page The session page to add.
   * @return false if page is null or a session with the same name already
   *         exists in this group.
   */
  bool AddSession(SessionPage *page);

  /**
   * Removes a session page by name from the group's bookkeeping. Does not
   * destroy the page window.
   *
   * @param name The name of the session to remove.
   * @return The removed session or nullptr.
   */
  SessionPage *RemoveSession(const wxString &name);

  /**
   * Applies a function to every session page in the group.
   */
  void Apply(std::function<void(SessionPage *)> func);

  const std::vector<SessionPage *> &GetSessions() const { return m_sessions; }
  inline size_t GetCount() const { return m_sessions.size(); }
  inline bool IsEmpty() const { return m_sessions.empty(); }

  int FindByName(const wxString &name) const;
  SessionPage *GetSessionByName(const wxString &name) const;

  inline bool IsTerminalsGroup() const { return m_terminalsGroup; }
  inline bool IsSessionGroup() const { return !IsTerminalsGroup(); }
  inline bool IsDefaultGroup() const { return GetGroupName() == _("Default"); }

  // The most recently shown session in this group; used when the user
  // selects the group's tree node directly rather than a session leaf.
  inline SessionPage *GetLastActive() const { return m_lastActive; }
  void SetLastActive(SessionPage *page) { m_lastActive = page; }

  // This group's own node in MainView's tree. Set once by MainView right
  // after the container item is created.
  inline const wxDataViewItem &GetContainerItem() const {
    return m_containerItem;
  }
  inline void SetContainerItem(const wxDataViewItem &item) {
    m_containerItem = item;
  }

private:
  wxString m_groupName;
  bool m_terminalsGroup{false};
  std::vector<SessionPage *> m_sessions;
  SessionPage *m_lastActive{nullptr};
  wxDataViewItem m_containerItem;
};
