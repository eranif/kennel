#pragma once

#include "core/Account.h"
#include "core/FindInFiles.h"
#include "core/Status.h"

#include <optional>
#include <vector>
#include <wx/panel.h>
#include <wx/string.h>

class wxAuiToolBar;
class wxCommandEvent;
class wxKeyEvent;
class wxMouseEvent;
class wxSetCursorEvent;
class wxStyledTextCtrl;
class wxUpdateUIEvent;

// The output of Find in Files: a toolbar (Stop, Clear) over a text with what
// was asked and what was found.
//
//   Searching for "needle"
//     In:      devbox: ~/proj
//     Flags:   match case, whole word
//     Include: *.cpp
//
//   src/a.cpp
//     12: the line with the needle in it
//     40: another needle
//   src/b/c.hpp
//     7: needle
//
//   3 matches in 2 files
//
// It runs the search itself (FindInFiles) and shows the matches as they come:
// a search of this machine or of WSL streams, a remote one shows all at the
// end. A new search replaces the results; one that runs is stopped first.
//
// A click on a match (or on a file) asks to open the file at that line, and so
// does Enter: a wxEVT_FILETREE_ACTIVATED FileEvent on the EventNotifier, the
// one that the file explorer sends, so whoever opens files for it opens these
// too. The view knows nothing about the pages. Nothing in the text can be
// selected: a click is for opening.
class FindResultsView : public wxPanel {
public:
  explicit FindResultsView(wxWindow *parent);
  ~FindResultsView() override;

  // Starts a search of `account` and shows what it finds. The error of
  // FindInFiles::Start() (no pattern, no folder).
  Status Search(const Account &account, const FindQuery &query);
  // Stops the search that runs (the matches found so far stay).
  void Stop();
  bool IsRunning() const;

  // The toolbar takes the style of the theme (see MainView).
  void ApplyToolBarStyle();

private:
  // What a line of the text is: the place of a match, or of a file (line 0).
  // A line of a message has no path.
  struct Row {
    wxString path;
    int line{0};
    int column{0};
    int length{0}; // The characters of the match, from the column
  };

  void OnBatch(FindEvent &event);
  void OnDone(FindEvent &event);
  void OnStop(wxCommandEvent &event);
  void OnStopUI(wxUpdateUIEvent &event);
  void OnClear(wxCommandEvent &event);
  void OnClearUI(wxUpdateUIEvent &event);
  void OnLeftDown(wxMouseEvent &event);
  void OnIgnoreMouse(wxMouseEvent &event);
  void OnSetCursor(wxSetCursorEvent &event);
  void OnKeyDown(wxKeyEvent &event);
  void OnThemeApplied(wxCommandEvent &event);

  // Gives the text the colours of the active theme.
  void ApplyTheme();
  // Empties the text (what the view knows about the results too).
  void Clear();
  // Appends the matches (the file lines are added where the file changes).
  void Append(const std::vector<FindMatch> &matches);
  // Appends a line of a message (what is asked, how it ended).
  void AppendMessage(const wxString &text, int style);
  // The message that opens a search: what is looked for, where, and the flags.
  void AppendQuery(const Account &account, const FindQuery &query);
  // The path of `path` below the folder of the search, for display.
  wxString Relative(const wxString &path) const;
  void Activate(int line);
  void Begin(const Account &account, const FindQuery &query);

  wxAuiToolBar *m_toolbar;
  wxStyledTextCtrl *m_text;
  FindInFiles m_search;

  // What the results on the screen are of.
  Account m_account;
  std::optional<FindQuery> m_query;
  // One for each line of the text (no row for the last, empty, line).
  std::vector<Row> m_rows;
  // The search whose answers the view shows (FindInFiles::GetGeneration()), or
  // 0 when it shows none: the events of another one are late answers of a
  // search that was replaced or cleared.
  int m_generation{0};
  size_t m_matchCount{0};
  size_t m_fileCount{0};
  wxString m_lastPath;

  // A search asked for while another one was running: it starts when that one
  // is done (its wxEVT_FIND_DONE comes after Stop()).
  struct Pending {
    Account account;
    FindQuery query;
  };
  std::optional<Pending> m_pending;
};
