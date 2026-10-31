#pragma once

#include "MainView.hpp"
#include "app/SessionStatusEvent.hpp"
#include "core/JobScheduler.h"
#include "core/UpdateChecker.h"

#include <wx/activityindicator.h>
#include <wx/aui/auibar.h>
#include <wx/frame.h>
#include <wx/statbmp.h>
#include <wx/timer.h>
#include <wx/toolbar.h>

#include <memory>
#include <vector>

// Top-level application window. Hosts a menu bar (File, Launch), a toolbar with
// one button per configured client/adapter (icon resolved from the adapter's
// iconPath), and the main view (session list + terminal area). Choosing a
// client from the Launch menu or toolbar opens the New Client Launch dialog
// with that client preselected. Shared state is reached via AppManager::Get().
class EditMenu;
class SessionPage;
class MainFrame : public wxFrame {
public:
  MainFrame();
  ~MainFrame() override;

  MainView *GetMainView() { return m_mainView; }

  JobScheduler *GetJobScheduler() { return m_jobScheduler.get(); }

  // A message in the main text of the status bar, until something else is
  // shown there (the help of a menu item uses it too). ClearActivityText()
  // brings back the text it had at the start.
  void SetActivityText(const wxString &text) {
    SetStatusText(text, kFieldText);
  }

  void ClearActivityText() { SetStatusText(m_baseText, kFieldText); }

  void StartActivityIndicator() {
    m_activityBusy = true;
    UpdateIndicator();
  }

  void StopActivityIndicator() {
    m_activityBusy = false;
    UpdateIndicator();
  }

  // The session page that is showing is not showing a session any more (a file
  // is): empties the fields of the session.
  void ClearSessionStatus();

  // What the editor page that is showing tells about its caret ("Ln 3, Col 7").
  // Shares the review field, which a file has no use for; ClearSessionStatus()
  // and the next session page's status replace it.
  void SetEditorStatus(const wxString &text) {
    SetStatusText(text, kFieldReview);
  }

  bool IsWindowActive(const SessionPage *win) const;

private:
  // Builds the menu bar: File -> Exit, Launch -> one item per adapter, and
  // View -> Theme -> [themes] + Change terminal font. Launch items reuse the
  // same wx ids as the toolbar tools (see kFirstClientToolId /
  // m_clientToolIds), so both share OnLaunchClient.
  void BuildMenuBar();

  void OnActivate(wxActivateEvent &event);

  // The status bar: the main text, the terminal title, the activity indicator,
  // the state of the review buddy, where the agent runs, and the icon and the
  // name of the session.
  enum StatusField {
    kFieldText,  // Activity messages and the help of menu items
    kFieldTitle, // The title of the terminal
    kFieldIndicator,
    kFieldReview,
    kFieldHost,
    kFieldIcon,
    kFieldSession,
    kFieldCount
  };
  void CreateStatusFields();
  void OnSessionStatus(SessionStatusEvent &event);
  void OnStatusBarSize(wxSizeEvent &event);
  // Puts the icon and the indicator, which are windows, over their fields.
  void LayoutStatusChildren();
  // Runs while an activity (saving a file, ...) or a review buddy is working.
  void UpdateIndicator();

  bool CheckIfCanStartAgent();

  // Appends search menu.
  void BuildSettingsMenu(wxMenuBar *menuBar);

  // Appends edit menu.
  void BuildEditMenu(wxMenuBar *menuBar);

  // Appends the Jobs menu.
  void BuildJobsMenu(wxMenuBar *menuBar);
  void OnManageJobs(wxCommandEvent &evt);
  void OnViewJobLog(wxCommandEvent &evt);

  void OnNextSession(wxCommandEvent &e);
  void OnPrevSession(wxCommandEvent &e);
  void OnPrevSessionUI(wxUpdateUIEvent &e);
  void OnNextSessionUI(wxUpdateUIEvent &e);
  // Find (and Replace, in the search bar of a file): the page that is
  // showing handles them.
  void OnFind(wxCommandEvent &e);
  void OnFindNext(wxCommandEvent &e);
  void OnFindUI(wxUpdateUIEvent &e);
  // Go to Line: the box on top of the file that is showing.
  void OnGotoLine(wxCommandEvent &e);
  void OnGotoLineUI(wxUpdateUIEvent &e);

  // Build the search menu
  void BuildSearchMenu(wxMenuBar *menuBar);

  // Appends the View menu.
  void BuildViewMenu(wxMenuBar *menuBar);
  // Puts the panes back to the layout they have without a saved one.
  void OnResetLayout(wxCommandEvent &e);

  // Opens the global config.json for editing
  void OnSettings(wxCommandEvent &evt);

  // Opens the edit hosts dialog
  void OnEditHosts(wxCommandEvent &evt);

  // Shows the About dialog
  void OnAbout(wxCommandEvent &evt);

  // Checks kennel-releases.json for a newer version. `silent` suppresses the
  // "you're up to date" / error dialogs, used for the on-startup check so it
  // stays quiet unless an update is actually found.
  void CheckForUpdates(bool silent);
  void OnCheckForUpdates(wxCommandEvent &evt);

  // Builds the toolbar, one tool per adapter. Tool ids are assigned
  // sequentially from kFirstClientToolId and map to m_clientToolIds.
  void BuildToolBar();
  // The work that waits until the window is up and running.
  void FinaliseStartup();
  void OnStartupTimer(wxTimerEvent &event);
#ifndef __WXMAC__
  // The theme changed: the toolbar takes its style.
  void OnToolBarStyle(wxCommandEvent &event);
#endif

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
  void OnRenameItemUI(wxUpdateUIEvent &event);
  void BuildLaunchTools();

  MainView *m_mainView{nullptr};
  wxActivityIndicator *m_statusIndicator{nullptr};
  wxStaticBitmap *m_sessionIcon{nullptr};
  wxString m_baseText; // The main text of the status bar without an activity
  bool m_activityBusy{false};
  bool m_sessionBusy{false};
  std::unique_ptr<UpdateChecker> m_updateChecker;
  std::unique_ptr<JobScheduler> m_jobScheduler;
  std::unique_ptr<EditMenu> m_editMenu;

  // Fires once, 500ms after the frame is constructed (see FinaliseStartup()).
  static constexpr int kStartupTimerId = wxID_HIGHEST + 100;
  wxTimer m_startupTimer;

  // Parallel to the toolbar tools: m_clientToolIds[i] is the adapter id for the
  // tool whose wx id is kFirstClientToolId + i.
  std::unordered_map<int, wxString> m_clientToolIdToName;

  // Parallel to the View -> Theme items: m_themeMenuNames[i] is the theme name
  // for the menu item whose wx id is kFirstThemeMenuId + i.
  std::vector<wxString> m_themeMenuNames;

#ifdef __WXMAC__
  // The native toolbar of the frame.
  wxToolBar *m_toolBar{nullptr};
#else
  wxAuiToolBar *m_toolBar{nullptr};
#endif
};

// Helper methods
MainFrame *GetMainFrame();
MainView *GetMainView();
wxEvtHandler *GetMainViewEventHandler();
