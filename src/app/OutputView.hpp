#pragma once

#include <wx/panel.h>
#include <wx/string.h>

class FindResultsView;
class wxAuiNotebook;
class wxCommandEvent;

// The output pane of the main window: a notebook with one tab for each kind of
// output. There is one now (the results of Find in Files); more come later,
// each as a tab with its own view. The pane itself (its place, whether it
// shows) belongs to MainView; this class is what is inside.
//
// The notebook is a wxAuiNotebook with the tab art of the application
// (GlyphTabArt): the colours of the theme, the tab outline colour and the tab
// height of the preferences.
class OutputView : public wxPanel {
public:
  // The names of the tabs, for SelectTab().
  static const wxString kFindInFilesTab;

  explicit OutputView(wxWindow *parent);
  ~OutputView() override;

  // The results of Find in Files.
  FindResultsView *GetFindInFiles() const { return m_findInFiles; }
  void SelectFindInFiles();
  // Shows the tab called `name`. False when there is none.
  bool SelectTab(const wxString &name);

  // The tabs take the theme and the preferences (outline, height).
  void ApplyPrefs();
  // The toolbars of the tabs take the style of the theme (see MainView).
  void ApplyToolBarStyle();

private:
  // wxEVT_THEME_APPLIED and wxEVT_APP_TAB_HEIGHT.
  void OnThemeApplied(wxCommandEvent &event);
  void OnTabHeight(wxCommandEvent &event);

  wxAuiNotebook *m_notebook;
  FindResultsView *m_findInFiles;
};
