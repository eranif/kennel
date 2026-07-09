#pragma once

#include "app/SessionPage.hpp"
#include "app/TabHistory.h"
#include "core/Status.h"
#include "core/Workspace.h"

#include <functional>
#include <wx/aui/auibook.h>
#include <wx/panel.h>

class SessionGroup : public wxPanel {
public:
  SessionGroup(wxWindow *parent, const wxString &groupName,
               bool terminalsGroup);
  ~SessionGroup();

  inline const wxString &GetGroupName() const { return m_groupName; }
  void SetGroupName(const wxString &groupName);

  /**
   * Creates a new session page based on the provided session definition.
   *
   * @param session The session configuration to use for the new page.
   * @param resume Whether the session should be resumed from a previous state.
   * @return A StatusOr containing the pointer to the newly created SessionPage,
   *         or an error if the agent is not found or the page could not be
   * added.
   */
  StatusOr<SessionPage *> NewSessionPage(const Session &session, bool resume);

  /**
   * Adds an existing session page to the group's notebook.
   *
   * @param page The session page to add.
   * @return Status::Ok() on success, or an error if the page is null or
   *         a session with the same name already exists.
   */
  Status AddSessionPage(SessionPage *page);

  /**
   * Removes a session page by its name from the group.
   *
   * @param name The name of the session to remove.
   * @return A StatusOr containing the pointer to the removed SessionPage,
   *         or an error if the session was not found.
   */
  StatusOr<SessionPage *> RemoveSessionPage(const wxString &name);

  /**
   * Applies a function to every session page in the group.
   *
   * @param func The function to apply to each SessionPage.
   */
  void Apply(std::function<void(SessionPage *)> func);

  /**
   * Restores sessions from the workspace settings.
   */
  void RestoreSessions();

  /**
   * Gets the currently active session page in the group.
   *
   * @return Pointer to the active SessionPage, or nullptr if no page is active.
   */
  SessionPage *GetActiveTerminal();
  inline size_t GetCount() const { return m_book->GetPageCount(); }
  inline bool IsEmpty() const { return GetCount() == 0; }

  /**
   * Selects a session page by its name.
   * @param sessionName The name of the session to select.
   */
  void SelectSession(const wxString &sessionName);

  /**
   * Selects the next or previous session page.
   * @param forward True to select the next page, false for the previous.
   */
  void SelectSession(bool forward);

  inline bool IsTerminalsGroup() const { return m_terminalsGroup; }
  inline bool IsSessionGroup() const { return !IsTerminalsGroup(); }

  void ApplyTheme(const wxString &themeName);
  void ApplyOptimizedDrawing();
  void ApplyFont(const wxFont &f);
  void RefreshAll();
  void RefreshSelection();

protected:
  void OnSessionExited(wxCommandEvent &e);
  void OnSessionIdle(wxCommandEvent &e);
  void OnSessionActive(wxCommandEvent &e);
  void OnIdleEvent(wxIdleEvent &e);
  bool DeleteByName(const wxString &name);
  int FindByName(const wxString &name) const;
  SessionPage *GetSessionByIndex(int index) const;

private:
  wxString m_groupName;
  wxAuiNotebook *m_book{nullptr};
  TabHistory m_history;
  int m_pendingIdle{0};
  bool m_idleHandled{false};
  bool m_terminalsGroup{false};
};
