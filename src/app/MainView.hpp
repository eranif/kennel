#pragma once

#include "UI.hpp"
#include "app/TabHistory.h"
#include "app/ThemeManager.h"
#include "core/AppPaths.h"
#include "core/UiPrefs.h"
#include "core/WorkspaceManager.h"

#include <wx/bmpbndl.h>
#include <wx/clntdata.h>
#include <wx/dataview.h>
#include <wx/timer.h>

#include <unordered_map>
#include <vector>

class SessionPage;

class AdapterRegistry;
class WorkspaceManager;
class UiPrefsStore;

static constexpr int kSpinnerFrameCount = 8;

// Blank placeholder shown when an agent group node is selected.
class GroupPage : public wxPanel {
public:
  explicit GroupPage(wxWindow *parent) : wxPanel(parent) {}
};

// Client data on each session leaf: points at the terminal page in simplebook.
class SessionItemData : public wxClientData {
public:
  explicit SessionItemData(SessionPage *page) : page{page} {}
  SessionPage *page{nullptr};
};

// Client data on each agent group container.
class GroupItemData : public wxClientData {
public:
  explicit GroupItemData(GroupPage *page, bool terminalsGroup = false)
      : page{page}, m_terminalsGroup{terminalsGroup} {}
  GroupPage *page{nullptr};
  inline bool IsTerminalsGroup() const { return m_terminalsGroup; }
  inline bool IsSessionGroup() const { return !IsTerminalsGroup(); }

private:
  bool m_terminalsGroup{false};
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
  void RefreshCurrentSelection();
  bool CanRefreshCurrent() const;
  const TabHistory &GetHistory() const { return m_history; }

  SessionPage *GetActiveTerminal();
  wxDataViewTreeCtrl *GetTree() { return m_dvListCtrlSessions; }

  void SelectSession(const wxString &sessionName);
  void SelectSession(bool forward);

  size_t SessionCount() const;

  // Prompts for confirmation, then closes every session in every group.
  void CloseAllSessions();
  bool IsSelectionSessionGroup() const;
  bool IsSelectionTerminalGroup() const;
  bool IsSelectionTerminal() const;
  bool IsSelectionSession() const;
  void RenameGroup(const wxDataViewItem &item);
  void RenameTerminal(const wxDataViewItem &item);
  wxString GetSelectedItemText() const;
  bool IsNameExist(const wxString &name) const;

protected:
  void OnContextMenu(wxDataViewEvent &event);
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnRenameEnded(wxDataViewEvent &event);
  void DoSetSession(const wxString &name);
  void OnSessionExited(wxCommandEvent &e);
  void OnSessionIdle(wxCommandEvent &e);
  void OnSessionActive(wxCommandEvent &e);
  void DeleteByName(const wxString &name);
  void DeleteAll();
  void DoGroupMenu(const wxDataViewItem &item);
  void Traverse(std::function<bool(const wxDataViewItem &)> visit) const;

  // Moves the session leaf `item` into `targetGroup` (created if needed),
  // reparenting it in the tree and retagging the workspace + live page.
  void MoveSessionToGroup(const wxDataViewItem &item,
                          const wxString &targetGroup);
  // Removes `group` (and its backing page) if it has no children, unless it is
  // the "Default" group, which must always exist.
  void RemoveGroupIfEmpty(const wxDataViewItem &group);
  SessionPage *GetSessionPage(const wxString &name);

private:
  void BuildTree();
  void BuildGroups();
  void LoadBitmaps();

  wxDataViewItem GroupNode(const wxString &groupName) const;
  wxDataViewItem EnsureGroup(const wxString &groupName);
  wxDataViewItem ItemFromName(const wxString &name) const;
  SessionPage *PageFromItem(const wxDataViewItem &item) const;
  GroupItemData *GetGroupItemData(const wxDataViewItem &item) const;
  bool IsTerminalNode(const wxDataViewItem &item) const;

  wxDataViewItem AddSessionLeaf(const wxString &groupName, const wxString &name,
                                SessionPage *page);
  std::vector<wxDataViewItem> SessionItemsInOrder() const;
  // Restores a session leaf to its agent's icon (used when the session is
  // not busy). No-op if the item has no resolvable agent.
  void SetAgentIcon(const wxDataViewItem &item);
  void SavePrefs();

  // Creates and adds a terminal page for an existing Session.
  SessionPage *AddSessionPage(const Session &session, bool resume);

  const AdapterRegistry *m_registry{nullptr};
  WorkspaceManager *m_workspace{nullptr};
  AppPaths m_paths;

  wxDataViewTreeCtrl *m_dvListCtrlSessions{nullptr};

  std::array<wxBitmapBundle, kSpinnerFrameCount> m_spinnerFrames;
  TabHistory m_history;
  int m_pendingIdle{0};
};
