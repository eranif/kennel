#pragma once

#include "core/Account.h"
#include "core/FindInFiles.h"
#include "core/Status.h"

#include <optional>
#include <vector>
#include <wx/panel.h>
#include <wx/string.h>

class wxButton;
class wxCommandEvent;
class wxKeyEvent;
class wxStaticText;
class wxStyledTextCtrl;
class wxStyledTextEvent;

// The output of Find in Files: the matches of a search, grouped by file.
//
//   Searching "needle" in ~/proj ...            [Stop]
//   src/a.cpp
//     12: the line with the needle in it
//     40: another needle
//   src/b/c.hpp
//     7: needle
//
// It runs the search itself (FindInFiles) and shows the matches as they come:
// a search of this machine or of WSL streams, a remote one shows all at the
// end. A new search replaces the results; one that runs is stopped first.
//
// Double click or Enter on a match (or on a file) asks to open the file at that
// line: a wxEVT_FILETREE_ACTIVATED FileEvent on the EventNotifier, the one that
// the file explorer sends, so whoever opens files for it opens these too. The
// view knows nothing about the pages.
class FindResultsView : public wxPanel {
public:
  explicit FindResultsView(wxWindow *parent);
  ~FindResultsView() override;

  // Starts a search of `account` and shows what it finds. The error of
  // FindInFiles::Start() (no pattern, no folder), or when the search that
  // runs could not be told to go away (see Stop()).
  Status Search(const Account &account, const FindQuery &query);
  // Stops the search that runs (the matches found so far stay).
  void Stop();
  bool IsRunning() const;

  // The query of the results that show, to start the next search from.
  const std::optional<FindQuery> &GetQuery() const { return m_query; }

private:
  // What a line of the text is: the place of a match, or of a file (line 0).
  struct Row {
    wxString path;
    int line{0};
    int column{0};
  };

  void OnBatch(FindEvent &event);
  void OnDone(FindEvent &event);
  void OnStop(wxCommandEvent &event);
  void OnStopUI(wxUpdateUIEvent &event);
  void OnDoubleClick(wxStyledTextEvent &event);
  void OnKeyDown(wxKeyEvent &event);
  void OnThemeApplied(wxCommandEvent &event);

  // Gives the text the colours of the active theme.
  void ApplyTheme();
  void Clear();
  // Appends the matches (the file lines are added where the file changes).
  void Append(const std::vector<FindMatch> &matches);
  // Sets the line above the text.
  void SetHeader(const wxString &text);
  // The path of `path` below the folder of the search, for display.
  wxString Relative(const wxString &path) const;
  void Activate(int line);
  void Begin(const Account &account, const FindQuery &query);

  wxStaticText *m_header;
  wxButton *m_stop;
  wxStyledTextCtrl *m_text;
  FindInFiles m_search;

  // What the results on the screen are of.
  Account m_account;
  std::optional<FindQuery> m_query;
  // One for each line of the text (no row for the last, empty, line).
  std::vector<Row> m_rows;
  // The search whose answers the view shows (FindInFiles::GetGeneration()):
  // the events of another one are late answers of a search that was replaced.
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
