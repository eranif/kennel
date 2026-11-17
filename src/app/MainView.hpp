#pragma once

#include "UI.hpp"
#include "app/FileEvent.hpp"
#include "app/SideView.hpp"
#include "app/ThemeManager.h"

class FindResultsView;

// The main window's content: the side bar (a notebook with the file explorer,
// git, the activity and the results of Find in Files) and its layout. The pages
// are in the notebook (MainNotebook), which this class does not know. The side
// views listen on the EventNotifier to what happens to the sessions and files,
// and send what the user asks to it themselves; this class only hosts them and
// lays them out.
class MainView : public MainViewBase {
public:
  explicit MainView(wxWindow *parent);
  ~MainView() override;

  // The layout (the AUI perspective: where the side bar is, and how wide, and
  // the layout of its tabs) is remembered between runs in the UI prefs.
  // RestoreLayout() applies it (once the window has its final size);
  // SaveLayout() records the current one into the prefs (the caller saves the
  // prefs).
  void RestoreLayout();
  void SaveLayout();
  // Puts the side bar back to the layout it had when the window was created
  // (on the right, a quarter of the window wide, the tabs as they are made):
  // what a start without a saved layout shows.
  void ResetLayout();
  // The side bar, with all the views as tabs.
  bool IsSideBarShown() const;
  // The side of the window that the side bar is on: wxLEFT or wxRIGHT. It
  // starts on the right.
  wxDirection GetSideBarPosition() const;
  void SetSideBarPosition(wxDirection position);
  void ShowSideBar(bool show);
  // Shows the side bar with the tab `tab` in front.
  void ShowSideTab(SideView::Tab tab);
  // The results of Find in Files (the "Find in Files" tab).
  FindResultsView *GetFindInFiles() const;

  const std::vector<LoadedTheme> &Themes() const {
    return ThemeManager::Get().Themes();
  }

private:
  void LoadBitmaps();
  void FinaliseConstruction();
  bool MakeActive(wxWindow *win);
  using ChildFocusEvent = void (MainView::*)(wxChildFocusEvent &);
  void BindFocusChildEvent(wxWindow *win, ChildFocusEvent handlerFn);

  void OnMainPanelChildFocus(wxChildFocusEvent &event);
  void OnFileExplorerPanelChildFocus(wxChildFocusEvent &event);
  // Redraws the panes (the active one changed) on the next pass of the event
  // loop, once for all the calls before it. Not at once: a focus change can
  // come while wxAuiManager::Update() runs (a pane is laid out, the window
  // becomes the key window), and a nested Update() frees the parts of the
  // layout that the running one still uses.
  void ScheduleAuiUpdate();

  // Gives the side bar its default width (kSidePaneWidthPercent of the window).
  void ApplySideBarDefaultWidth();

  // ---- The theme changed, and the file explorer's files ---------------------
  void OnToolBarStyle(wxCommandEvent &event);
  // A file or a folder of the file explorer was double clicked: passed on.
  void OnFileTreeActivated(FileEvent &event);
  void OnAppPrefsSaved(wxCommandEvent &event);
  void ApplyPrefs();

  SideView *m_sideView{nullptr};
  bool m_layoutRestored{false};
  // An update of the panes is scheduled (ScheduleAuiUpdate()).
  bool m_auiUpdatePending{false};
  // The AUI perspective of the panes as they are created (see ResetLayout()).
  wxString m_defaultLayout;
};
