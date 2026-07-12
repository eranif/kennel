#pragma once

#include "app/SessionPage.hpp"
#include "app/TabHistory.h"
#include "core/Status.h"
#include "core/Workspace.h"

#include <functional>
#include <vector>
#include <wx/aui/auibook.h>
#include <wx/panel.h>

class SessionGroupEvent : public wxCommandEvent {
public:
  SessionGroupEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}

  SessionGroupEvent(const SessionGroupEvent &other) = default;
  wxEvent *Clone() const override { return new SessionGroupEvent(*this); }
  void SetGroupName(const wxString &groupName) {
    this->m_groupName = groupName;
  }
  void SetNewGroupName(const wxString &newGroupName) {
    this->m_newGroupName = newGroupName;
  }
  const wxString &GetGroupName() const { return m_groupName; }
  const wxString &GetNewGroupName() const { return m_newGroupName; }

  void SetSessionName(const wxString &sessionName) {
    this->m_sessionName = sessionName;
  }
  const wxString &GetSessionName() const { return m_sessionName; }

private:
  wxString m_groupName;
  wxString m_newGroupName;
  wxString m_sessionName;
};

wxDECLARE_EVENT(wxEVT_GROUP_PAGE_CHANGED, SessionGroupEvent);
wxDECLARE_EVENT(wxEVT_GROUP_LAST_PAGE_CLOSED, SessionGroupEvent);
wxDECLARE_EVENT(wxEVT_GROUP_MOVE_TO_GROUP, SessionGroupEvent);

class SessionGroup : public wxPanel {
public:
  SessionGroup(wxWindow *parent, const wxString &groupName,
               bool terminalsGroup);
  ~SessionGroup();

  inline const wxString &GetGroupName() const { return m_groupName; }
  void SetGroupName(const wxString &groupName);

  wxString Rename();

  /**
   * Creates a new session page based on the provided session definition.
   *
   * @param session The session configuration to use for the new page.
   * @param resume Whether the session should be resumed from a previous state.
   * @return A StatusOr containing the pointer to the newly created SessionPage,
   *         or an error if the agent is not found or the page could not be
   * added.
   */
  SessionPage *NewSessionPage(const Session &session, bool resume);

  /**
   * Adds an existing session page to the group's notebook.
   *
   * @param page The session page to add.
   * @return Status::Ok() on success, or an error if the page is null or
   *         a session with the same name already exists.
   */
  bool AddSessionPage(SessionPage *page);

  /**
   * Removes a session page by its name from the group.
   *
   * @param name The name of the session to remove.
   * @return The removed session or nullptr.
   */
  SessionPage *RemoveSessionPage(const wxString &name);

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

  std::vector<SessionPage *> GetAllSessions() const;

  /**
   * Gets the currently active session page in the group.
   *
   * @return Pointer to the active SessionPage, or nullptr if no page is active.
   */
  SessionPage *GetActivePage();
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
  inline bool IsDefaultGroup() const { return GetGroupName() == _("Default"); }

  void ApplyTheme(const wxString &themeName);
  void ApplyOptimizedDrawing();
  void ApplyFont(const wxFont &f);
  void RefreshAll();
  void RefreshSelection();
  void CloseAll();
  void CloseActiveSession();
  void RenameActiveTerminal();

protected:
  void OnSessionExited(wxCommandEvent &e);
  void OnSessionIdle(wxCommandEvent &e);
  void OnSessionActive(wxCommandEvent &e);
  void OnIdleEvent(wxIdleEvent &e);
  void NotifyLastPageClosedIfEmpty();
  void CloseSession(const wxString &sessionName, int index);

  void OnPageChanged(wxAuiNotebookEvent &event);
  void OnPageClosed(wxAuiNotebookEvent &event);
  void OnContextMenu(wxAuiNotebookEvent &event);
  void OnTabMiddleClick(wxAuiNotebookEvent &event);

  bool DeleteSessionByName(const wxString &name);
  int FindByName(const wxString &name) const;
  SessionPage *GetSessionByIndex(int index) const;
  SessionPage *GetSessionByName(const wxString &name) const;
  void RenameTerminal(int tabIdx);

private:
  wxString m_groupName;
  wxAuiNotebook *m_book{nullptr};
  int m_pendingIdle{0};
  bool m_idleHandled{false};
  bool m_terminalsGroup{false};
};
