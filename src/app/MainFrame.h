#pragma once

#include "MainView.hpp"

#include <wx/activityindicator.h>
#include <wx/aui/auibar.h>
#include <wx/frame.h>

#include <vector>

// Top-level application window. Hosts a menu bar (File, Launch), a toolbar with
// one button per configured client/adapter (icon resolved from the adapter's
// iconPath), and the main view (session tree + terminal area). Choosing a
// client from the Launch menu or toolbar opens the New Client Launch dialog
// with that client preselected. Shared state is reached via AppManager::Get().
class MainFrame : public wxFrame {
public:
  MainFrame();
  ~MainFrame() override;

  // Captures Ctrl-Tab / Ctrl-Shift-Tab before child controls consume them.
  void HandleCtrlTabNavigation(wxKeyEvent &evt);
  MainView *GetMainView() { return m_mainView; }

  void SetActivityText(const wxString &text) { SetStatusText(text, 0); }

  void ClearActivityText() { SetStatusText(wxEmptyString, 0); }

  void StartActivityIndicator() {
    if (m_statusIndicator) {
      m_statusIndicator->Show();
      m_statusIndicator->Start();
    }
  }

  void StopActivityIndicator() {
    if (m_statusIndicator) {
      m_statusIndicator->Stop();
      m_statusIndicator->Hide();
    }
  }

private:
  // Builds the menu bar: File -> Exit, Launch -> one item per adapter, and
  // View -> Theme -> [themes] + Change terminal font. Launch items reuse the
  // same wx ids as the toolbar tools (see kFirstClientToolId /
  // m_clientToolIds), so both share OnLaunchClient.
  void BuildMenuBar();

  void OnActivate(wxActivateEvent &event);

  bool CheckIfCanStartAgent();

  // Appends search menu.
  void BuildSettingsMenu(wxMenuBar *menuBar);

  // Appends edit menu.
  void BuildEditMenu(wxMenuBar *menuBar);

  void OnNextSession(wxCommandEvent &e);
  void OnPrevSession(wxCommandEvent &e);
  void OnPrevSessionUI(wxUpdateUIEvent &e);
  void OnNextSessionUI(wxUpdateUIEvent &e);

  // Build the search menu
  void BuildSearchMenu(wxMenuBar *menuBar);

  // Opens the global config.json for editing
  void OnSettings(wxCommandEvent &evt);

  // Opens the edit hosts dialog
  void OnEditHosts(wxCommandEvent &evt);

  // Shows the About dialog
  void OnAbout(wxCommandEvent &evt);

  // Enable optimized drawing for all terminals.
  void OnEnableOptimizedDrawing(wxCommandEvent &evt);

  // Builds the toolbar, one tool per adapter. Tool ids are assigned
  // sequentially from kFirstClientToolId and map to m_clientToolIds.
  void BuildToolBar();

  // Persists the window geometry to .persist.json on close.
  void OnClose(wxCloseEvent &evt);

  // Opens the launch dialog with the adapter for the event's id preselected.
  // Shared by the Launch menu items and the toolbar tools.
  void OnStartAgentFromToolBar(wxCommandEvent &evt);
  void OnEditAgents(wxCommandEvent &evt);
  void OnRefreshSession(wxCommandEvent &evt);
  void OnRefreshSessionUI(wxUpdateUIEvent &evt);
  void OnNewAgent(wxCommandEvent &evt);
  void OnNewTerminal(wxCommandEvent &evt);
  void OnCloseAllSessions(wxCommandEvent &evt);
  void OnCloseAllSessionsUI(wxUpdateUIEvent &evt);
  void OnStartAgent(wxCommandEvent &evt);
  void OnRenameItem(wxCommandEvent &event);
  void BuildLaunchTools();

  MainView *m_mainView{nullptr};
  wxActivityIndicator *m_statusIndicator{nullptr};

  // Parallel to the toolbar tools: m_clientToolIds[i] is the adapter id for the
  // tool whose wx id is kFirstClientToolId + i.
  std::unordered_map<int, wxString> m_clientToolIdToName;

  // Parallel to the View -> Theme items: m_themeMenuNames[i] is the theme name
  // for the menu item whose wx id is kFirstThemeMenuId + i.
  std::vector<wxString> m_themeMenuNames;

#ifdef __WXMSW__
  wxAuiToolBar *m_toolBar{nullptr};
#else
  wxToolBar *m_toolBar{nullptr};
#endif
};

// Helper methods
MainFrame *GetMainFrame();
