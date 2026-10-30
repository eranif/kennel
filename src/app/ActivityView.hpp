#pragma once

#include "app/FileEvent.hpp"
#include "app/PageViewEvent.hpp"
#include "core/Spinner.h"
#include "core/Workspace.h"

#include <optional>
#include <vector>
#include <wx/panel.h>
#include <wx/variant.h>

class wxDataViewEvent;
class wxDataViewListCtrl;

// Sent on the EventNotifier when the user clicks a session of the list. The
// session is GetRef(); the host shows it, and the page that shows then comes
// back on wxEVT_SESSION_PAGE_SHOWN.
wxDECLARE_EVENT(wxEVT_ACTIVITYVIEW_SESSION_SELECTED, PageViewEvent);

// The sessions whose program is working, is done or is blocked.
//
//   (icon)  Session        Group
//   hand    build          Development
//   check   tests          Development
//   spin    docs           General
//
// Blocked comes first (the user is needed), then Done, then Working; the newest
// first in each. A working or blocked row is always there, also for the session
// that is showing, and goes when the program stops (SetActivity() with None) or
// the session is closed. A done row goes when the session is the one the user
// works in: the view checks it when the application is idle, so a done that
// comes while the user is in that session goes at once, and one for another
// session goes when the user selects it. The spinner of a working row turns.
//
// Self contained: it listens on the EventNotifier to what happens to the
// sessions and sends the click there; it never learns what the host does with
// it.
class ActivityView : public wxPanel {
public:
  explicit ActivityView(wxWindow *parent);
  ~ActivityView() override;

  // The program of `ref` is doing `activity` now. A row is added or changed;
  // PageActivity::None removes it, but not a done.
  void SetActivity(const SessionRef &ref, PageActivity activity);
  size_t GetRowCount() const { return m_rows.size(); }

private:
  void OnFileReveal(FileEvent &event);
  void OnSessionPageShown(PageViewEvent &event);
  void OnSessionRenamed(PageViewEvent &event);
  void OnSessionPageRemoved(PageViewEvent &event);
  void OnSessionPagesCleared(wxCommandEvent &event) {
    event.Skip();
    Clear();
  }
  void OnSessionPageActivity(PageViewEvent &event);

  void Clear();
  // A session got another name or group. Its row stays.
  void RenameSession(const SessionRef &from, const SessionRef &to);
  // The session is gone.
  void RemoveSession(const SessionRef &ref);

  struct Row {
    SessionRef ref;
    PageActivity activity{PageActivity::None};
    // Grows with each change: the newest has the biggest.
    int serial{0};
  };

  // The columns: the icon of the status (a fixed width), then the session and
  // the group, which fit their content.
  static constexpr unsigned int kStatusColumn = 0;
  static constexpr unsigned int kFirstFitColumn = 1;
  static constexpr int kStatusColumnWidthDip = 32;

  int FindRow(const SessionRef &ref) const;
  // Sorts the rows and shows them; the spinner runs while a row is working.
  void Refill();
  // The text and the icon of the status of `row`.
  wxVariant StatusCell(const Row &row) const;
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnIdle(wxIdleEvent &event);
  // The spinner turned: the working rows get the new frame.
  void OnSpinnerFrame();

  wxDataViewListCtrl *m_list{nullptr};
  // In the order of the list.
  std::vector<Row> m_rows;
  int m_serial{0};
  std::optional<SessionRef> m_active;
  Spinner m_spinner;
};
