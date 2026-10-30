#pragma once

#include "UI.hpp"
#include "app/FileEvent.hpp"
#include "app/ThemeManager.h"

#include <span>

class ActivityView;
class FileExplorerView;

// The main window's content: the side panes (the file explorer, the activity
// view) and their layout. The pages are in the notebook (MainNotebook), which
// this class does not know. The side views listen on the EventNotifier to what
// happens to the sessions and files, and send what the user asks to it
// themselves; this class only hosts them and lays them out.
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
  void FinaliseConstruction();
  bool MakeActive(wxWindow *win);
  using ChildFocusEvent = void (MainView::*)(wxChildFocusEvent &);
  void BindFocusChildEvent(wxWindow *win, ChildFocusEvent handlerFn);

  void OnMainPanelChildFocus(wxChildFocusEvent &event);
  void OnActivityPanelChildFocus(wxChildFocusEvent &event);
  void OnFileExplorerPanelChildFocus(wxChildFocusEvent &event);

  // Gives the side panes `names` their default width (kSidePaneWidthDip).
  void ApplyDefaultPaneWidths(std::span<const wxString> names);

  // ---- The theme changed, and the file explorer's files ---------------------
  void OnToolBarStyle(wxCommandEvent &event);
  // A file or a folder of the file explorer was double clicked: passed on.
  void OnFileTreeActivated(FileEvent &event);

  FileExplorerView *m_fileExplorer{nullptr};
  ActivityView *m_activityView{nullptr};
  bool m_layoutRestored{false};
  // The AUI perspective of the panes as they are created (see ResetLayout()).
  wxString m_defaultLayout;
};
