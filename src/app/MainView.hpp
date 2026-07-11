#pragma once

#include "UI.hpp"
#include "app/AcceleratorInterceptor.h"
#include "app/SessionGroup.h"
#include "app/ThemeManager.h"
#include "core/AppPaths.h"
#include "core/WorkspaceManager.h"

#include <wx/bmpbndl.h>
#include <wx/clntdata.h>
#include <wx/dataview.h>
#include <wx/timer.h>

#include <vector>

class SessionPage;

class AdapterRegistry;
class WorkspaceManager;
class UiPrefsStore;

static constexpr int kSpinnerFrameCount = 8;

// Client data on each agent group container.
class GroupItemData {
public:
  explicit GroupItemData(const wxString &n, SessionGroup *p)
      : groupName{n}, groupPage{p} {}
  wxString groupName;
  SessionGroup *groupPage{nullptr};
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

  bool LaunchSession(const NewSessionRequest &req);

  // Shows the Start Agent dialog, then launches on OK. `agentName` preselects
  // an agent (empty -> first defined agent); `groupName` pre-sets the group
  // field (empty -> dialog default, "Default").
  void StartAgent(const wxString &agentName = wxEmptyString,
                  const wxString &groupName = wxEmptyString);

  // Shows a plain terminal.
  void StartTerminal();

  // Rebuilds UI from sessions persisted in workspace.json.
  void RestoreSessions();

  const std::vector<LoadedTheme> &Themes() const {
    return ThemeManager::Get().Themes();
  }

  void ApplyTheme(const wxString &themeName);
  void ApplyOptimizedDrawing();
  void ApplyFont(const wxFont &f);
  void RefreshSelectedGroup();
  bool CanRefreshCurrent() const;
  void RefreshCurrentSelection();

  SessionPage *GetActiveTerminal();

  void SelectSession(const wxString &sessionName);
  void SelectSession(bool forward);

  size_t SessionCount() const;

  // Prompts for confirmation, then closes every session in every group.
  void CloseAllSessions();
  bool IsSelectionSessionGroup() const;
  bool IsSelectionTerminalGroup() const;
  void RenameSelectedGroup();
  bool IsNameExist(const wxString &name) const;
  SessionGroup *GetSelectedGroup() const;

protected:
  void DoSelectGroup(const wxDataViewItem &item);
  void DoSelectGroup(const wxString &name);
  void OnContextMenu(wxDataViewEvent &event);
  void OnSelectionChanged(wxDataViewEvent &event) override;
  void OnGroupPageChanged(wxCommandEvent &event);
  void OnGroupLastPageClosed(wxCommandEvent &event);
  void DeleteGroupByName(const wxString &name);
  void DeleteAll();
  void DoGroupMenu(const wxDataViewItem &item);
  void Traverse(std::function<bool(SessionPage *)> visit) const;
  std::vector<SessionPage *> GetAllSessions() const;
  std::vector<SessionGroup *> GetAllGroups() const;

  // Removes `group` (and its backing page) if it has no children, unless it is
  // the "Default" group, which must always exist.
  void RemoveGroupIfEmpty(const wxDataViewItem &group);

private:
  void LoadBitmaps();

  SessionGroup *EnsureGroup(const wxString &groupName);
  GroupItemData *GetGroupItemData(const wxDataViewItem &item) const;
  SessionGroup *GetSessionGroup(const wxString &name) const;
  SessionGroup *GetSessionGroup(int row) const;
  SessionPage *AddSession(SessionPage *page);
  wxDataViewItem GetSessionGroupItem(const wxString &name);
  void SavePrefs();

  // Creates and adds a terminal page for an existing Session.
  SessionPage *AddSessionPage(const Session &session, bool resume);

  const AdapterRegistry *m_registry{nullptr};
  WorkspaceManager *m_workspace{nullptr};
  AppPaths m_paths;

  std::array<wxBitmapBundle, kSpinnerFrameCount> m_spinnerFrames;
  int m_pendingIdle{0};
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor{nullptr};
  bool m_idleHandled{false};
};
