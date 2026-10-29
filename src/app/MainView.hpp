#pragma once

#include "UI.hpp"
#include "app/FileEvent.hpp"
#include "app/PageViewEvent.hpp"
#include "app/ThemeManager.h"

#include <span>

class ActivityView;
class FileExplorerView;

// The main window's content: the side panes (the file explorer, the activity
// view) and their layout. The pages are in the notebook (MainNotebook), which
// this class does not know: the two talk with events only. The notebook tells
// what happens to the sessions (PageViewEvent.hpp, wxEVT_FILE_REVEAL) and this
// class shows it in the side views; what the user asks in them is passed on
// through the EventNotifier.
class MainView : public MainViewBase {
public:
  explicit MainView(wxWindow *parent);
  ~MainView() override = default;

  // The layout of the panes (the AUI perspective) is remembered between runs in
  // the UI prefs. RestoreLayout() applies it (once the window has its final
  // size); SaveLayout() records the current one into the prefs (the caller
  // saves the prefs).
  void RestoreLayout();
  void SaveLayout();
  // Puts the panes back to the layout they had when the window was created,
  // with the default width of the side panes: what a start without a saved
  // layout shows.
  void ResetLayout();

  const std::vector<LoadedTheme> &Themes() const {
    return ThemeManager::Get().Themes();
  }

private:
  void LoadBitmaps();
  // Gives the side panes `names` their default width (kSidePaneWidthDip).
  void ApplyDefaultPaneWidths(std::span<const wxString> names);

  // ---- From the pages: the file explorer follows ---------------------------
  void OnSessionPageAdded(PageViewEvent &event);
  void OnSessionPageRemoved(PageViewEvent &event);
  void OnSessionPageRenamed(PageViewEvent &event);
  void OnSessionPagesCleared(wxCommandEvent &event);
  void OnSessionPageShown(PageViewEvent &event);
  void OnSessionPageActivity(PageViewEvent &event);
  void OnFileReveal(FileEvent &event);
  void OnToolBarStyle(wxCommandEvent &event);

  // ---- From the file explorer: passed on to whoever cares -------------------
  // The user chose a session in the choice of the file explorer.
  void OnExplorerSession(PageViewEvent &event);
  // A file or a folder of the file explorer was double clicked.
  void OnFileTreeActivated(FileEvent &event);

  // ---- From the activity view: passed on to whoever cares -------------------
  // The user clicked a session of the activity view.
  void OnActivitySession(PageViewEvent &event);

  FileExplorerView *m_fileExplorer{nullptr};
  ActivityView *m_activityView{nullptr};
  bool m_layoutRestored{false};
  // The AUI perspective of the panes as they are created (see ResetLayout()).
  wxString m_defaultLayout;
};
