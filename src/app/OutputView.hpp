#pragma once

#include "UI.hpp"
#include "app/FileEvent.hpp"
#include "app/GitTerminals.hpp"

#include <memory>

class FindResultsView;

// The "Output View": the pane at the bottom of the window, as wide as all of it
// (MainView docks it). It is a notebook of the outputs of the application, one
// tab each (the layout is OutputViewBase, in UI.wxcp):
//
//   Git: the terminals of the git panels of the side bar (GitTerminals), the
//        one of the git panel that shows.
//   Find in Files: the results of the last search (FindResultsView).
//
// It knows nothing of the git panels: it acts on the events they send on the
// EventNotifier (wxEVT_GITVIEW_RUN_IN_TERMINAL, wxEVT_GITVIEW_SHOWN,
// wxEVT_GITVIEW_CLOSED). Showing the pane is the host's (MainView).
class OutputView : public OutputViewBase {
public:
  explicit OutputView(wxWindow *parent);
  ~OutputView() override;

  enum class Tab { kGit, kFindInFiles };
  // Brings the tab to the front.
  void SelectTab(Tab tab);
  // The results of Find in Files (a tab).
  FindResultsView *GetFindInFiles() const { return m_findInFiles; }
  // Gives the toolbars of the tabs the style of the theme of the application.
  void ApplyToolBarStyle();
  // Takes the theme, the outline colour of the tabs and their height from the
  // prefs, as the side bar does (call it when they change; a change of the
  // theme does it too).
  void ApplyPrefs();

private:
  void OnThemeApplied(wxCommandEvent &event);
  void OnTabHeight(wxCommandEvent &event);
  // What the git panels ask of their terminals. A command brings the Git tab
  // to the front.
  void OnGitViewRunInTerminal(FileEvent &event);
  void OnGitViewShown(FileEvent &event);
  void OnGitViewClosed(FileEvent &event);

  // The terminals of the git panels, in m_bookGitTerminals.
  std::unique_ptr<GitTerminals> m_gitTerminals;
  FindResultsView *m_findInFiles{nullptr};
};
