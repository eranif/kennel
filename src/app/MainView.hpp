#pragma once

#include "UI.hpp"
#include "app/AcceleratorInterceptor.h"
#include "app/SessionGroup.h"
#include "app/ThemeManager.h"
#include "core/AppPaths.h"
#include "core/Job.h"
#include "core/WorkspaceManager.h"

#include <wx/bmpbndl.h>
#include <wx/clntdata.h>
#include <wx/dataview.h>
#include <wx/timer.h>

#include <map>
#include <memory>
#include <vector>

class SessionPage;

class AdapterRegistry;
class WorkspaceManager;
class UiPrefsStore;

static constexpr int kSpinnerFrameCount = 8;

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
// window is owned by m_sessionsBook.
class SessionItemData : public wxClientData {
public:
  explicit SessionItemData(SessionPage *p) : page{p} {}
  SessionPage *page{nullptr};
};

class SpinnerRenderer : public wxEvtHandler {
public:
  SpinnerRenderer(wxDataViewTreeCtrl *treeCtrl,
                  const std::array<wxBitmapBundle, kSpinnerFrameCount> &frames,
                  const wxDataViewItem &item)
      : m_treeCtrl{treeCtrl}, m_item{item}, m_frames{frames} {
    m_timer.SetOwner(this);
    m_timer.Start(100);
    Bind(wxEVT_TIMER, &SpinnerRenderer::OnTimer, this, m_timer.GetId());
  }

  ~SpinnerRenderer() override {
    m_timer.Stop();
    Unbind(wxEVT_TIMER, &SpinnerRenderer::OnTimer, this, m_timer.GetId());
    if (m_item.IsOk()) {
      m_treeCtrl->SetItemIcon(m_item, wxBitmapBundle{});
    }
  }

  void OnTimer(wxTimerEvent &event) {
    if (!m_item.IsOk() || !m_treeCtrl->GetItemData(m_item)) {
      m_timer.Stop();
      return;
    }
    m_treeCtrl->SetItemIcon(m_item, m_frames[m_frameIdx]);
    m_frameIdx = (m_frameIdx + 1) % kSpinnerFrameCount;
  }

private:
  wxDataViewTreeCtrl *m_treeCtrl{nullptr};
  wxDataViewItem m_item;
  wxTimer m_timer;
  int m_frameIdx{0};
  const std::array<wxBitmapBundle, kSpinnerFrameCount> &m_frames;
};

class MainView : public MainViewBase {
public:
  explicit MainView(wxWindow *parent);
  ~MainView() override;

  // `selectAfterLaunch` false keeps the current focus/selection untouched
  // (used for job runs, which shouldn't steal focus from whatever the user
  // is doing when the timer fires or "Run Now" is clicked).
  bool LaunchSession(const NewSessionRequest &req, bool selectAfterLaunch = true);

  // Shows the Start Agent dialog, then launches on OK. `agentName` preselects
  // an agent (empty -> first defined agent); `groupName` pre-sets the group
  // field (empty -> dialog default, "Default").
  void StartAgent(const wxString &agentName = wxEmptyString,
                  const wxString &groupName = wxEmptyString);

  // Shows a plain terminal.
  void StartTerminal();

  // Launches a one-shot session running `job`'s command/prompt, under a
  // "Jobs" group. Never persisted to workspace.json.
  void RunJob(const JobDef &job);

  // Rebuilds UI from sessions persisted in workspace.json.
  void RestoreSessions();

  const std::vector<LoadedTheme> &Themes() const {
    return ThemeManager::Get().Themes();
  }

  void ApplyPrefs();
  void ApplyTheme(const wxString &themeName);
  void ApplyOptimizedDrawing();
  void ApplyFont(const wxFont &f);
  void RefreshSelectedGroup();
  bool CanRefreshCurrent() const;
  void RefreshCurrentSelection();

  void SelectSession(const wxString &sessionName);

  // Cycles to the next/previous session across all groups, in tree order.
  void SelectSession(bool forward);

  size_t SessionCount() const;
  size_t GroupCount() const;

  // Prompts for confirmation, then closes every session in every group.
  void CloseAllSessions();
  bool IsSelectionSessionGroup() const;
  bool IsSelectionTerminalGroup() const;
  void RenameItem();
  bool IsNameExist(const wxString &name) const;
  SessionGroup *GetSelectedGroup() const;

  // The single SessionPage currently shown on the right, or nullptr.
  SessionPage *GetActiveSessionPage() const;

protected:
  void DoSelectGroup(const wxDataViewItem &item);
  void DoSelectGroup(const wxString &name);
  void OnContextMenu(wxDataViewEvent &event) override;
  void OnSelectionChanged(wxDataViewEvent &event) override;
  void OnSessionIdle(wxCommandEvent &e);
  void OnSessionActive(wxCommandEvent &e);
  void OnSessionExited(wxCommandEvent &e);
  void OnIdleEvent(wxIdleEvent &e);
  void DeleteGroupByName(const wxString &name);
  void DeleteAll();
  void DoGroupMenu(const wxDataViewItem &item);
  void DoSessionMenu(const wxDataViewItem &item);
  void RenameGroup(SessionGroup *group);
  void RenameSession(SessionPage *page);
  // Opens the Start Agent dialog pre-filled with `page`'s agent and group,
  // and an auto-generated unique name, so the user launches a fresh,
  // independent session cloned from it.
  void DuplicateSession(SessionPage *page);
  void RefreshGroup(SessionGroup *group);
  void CloseSession(SessionGroup *group, const wxString &sessionName);
  // Re-resolves `sessionName` to its group and closes it. Callers reached
  // from a menu/native callback must go through this via CallAfter rather
  // than calling CloseSession directly — deleting the session's tree leaf
  // (and possibly its now-empty parent group) synchronously from inside such
  // a callback can crash the native macOS outline view mid-redraw.
  void CloseSessionByName(const wxString &sessionName);
  // Shows some session after the active one is removed: prefers a
  // sibling in `preferredGroup`, else the first session in any group.
  void SelectFallbackSession(SessionGroup *preferredGroup);
  void Traverse(std::function<bool(SessionPage *)> visit) const;
  std::vector<SessionPage *> GetAllSessions() const;
  std::vector<SessionGroup *> GetAllGroups() const;
  void RemoveEmptyGroups();

private:
  void LoadBitmaps();

  SessionGroup *EnsureGroup(const wxString &groupName);
  GroupItemData *GetGroupItemData(const wxDataViewItem &item) const;
  SessionItemData *GetSessionItemData(const wxDataViewItem &item) const;
  SessionGroup *GetSessionGroup(const wxString &name) const;
  wxDataViewItem FindLeafItem(SessionGroup *group, SessionPage *page) const;

  // Attaches an already-constructed SessionPage to its group: adds it to
  // the group's session list, m_sessionsBook, and a new tree leaf.
  SessionPage *AddSession(SessionPage *page);

  // Makes `page` the one visible session: selects its leaf in the tree,
  // shows it in m_sessionsBook, and remembers it as its group's last-active.
  void SelectSessionPage(SessionPage *page);

  // Re-selects the currently active session's leaf in the tree, undoing a
  // selection change (e.g. after a group node was clicked to expand/collapse
  // it) without touching m_sessionsBook.
  void RestoreActiveSessionSelection();

  void MoveSessionToGroup(const wxString &sessionName,
                          const wxString &fromGroupName,
                          const wxString &toGroupName);

  void SavePrefs();

  // Creates and adds a terminal/agent page for an existing Session.
  SessionPage *AddSessionPage(const Session &session, bool resume);

  const AdapterRegistry *m_registry{nullptr};
  WorkspaceManager *m_workspace{nullptr};
  AppPaths m_paths;

  // Per-job run counter (job name -> next sequence number), so consecutive
  // runs of the same job get distinct tab names ("Test Job #1", "#2", ...)
  // instead of colliding with a still-open previous run's tab.
  std::map<wxString, int> m_jobRunCounters;

  std::array<wxBitmapBundle, kSpinnerFrameCount> m_spinnerFrames;
  int m_pendingIdle{0};
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor{nullptr};
  bool m_idleHandled{false};
};
