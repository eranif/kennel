#pragma once

#include "app/PageViewEvent.hpp"
#include "core/Workspace.h"

#include <optional>
#include <vector>
#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/variant.h>

class wxDataViewEvent;
class wxDataViewListCtrl;

// Sent (to the parent windows) when the user clicks a session of the list. The
// session is GetRef(); the host shows it and tells the view with
// SetActiveSession().
wxDECLARE_EVENT(wxEVT_ACTIVITYVIEW_SESSION_SELECTED, PageViewEvent);

// The sessions whose program is working, is done or is blocked.
//
//   Session        Group        Status
//   build          Development  Blocked
//   tests          Development  Done
//   docs           General      Working
//
// Blocked comes first (the user is needed), then Done, then Working; the newest
// first in each. A working or blocked row is always there, also for the session
// that is showing, and goes when the program stops (SetActivity() with None) or
// the session is closed. A done row goes when the session is the one the user
// works in: the view checks it when the application is idle, so a done that
// comes while the user is in that session goes at once, and one for another
// session goes when the user selects it. The spinner of a working row turns.
//
// Self contained: the host (MainView) says what happens to the sessions and
// listens to the click, which reaches the parent windows; it never learns what
// the host does with it.
class ActivityView : public wxPanel {
public:
  explicit ActivityView(wxWindow *parent);
  ~ActivityView() override = default;

  // The program of `ref` is doing `activity` now. A row is added or changed;
  // PageActivity::None removes it, but not a done.
  void SetActivity(const SessionRef &ref, PageActivity activity);
  // The session the user works in (none: a file, for example). Never sends
  // events; its done row goes when the application is idle.
  void SetActiveSession(const std::optional<SessionRef> &ref);
  // The session is gone.
  void RemoveSession(const SessionRef &ref);
  void RenameSession(const SessionRef &from, const SessionRef &to);
  void Clear();
  size_t GetRowCount() const { return m_rows.size(); }

private:
  struct Row {
    SessionRef ref;
    PageActivity activity{PageActivity::None};
    // Grows with each change: the newest has the biggest.
    int serial{0};
  };

  static constexpr int kSpinnerFrameCount = 8;
  static constexpr int kSpinnerMs = 100;

  int FindRow(const SessionRef &ref) const;
  // Sorts the rows and shows them; the spinner runs while a row is working.
  void Refill();
  // The text and the icon of the status of `row`.
  wxVariant StatusCell(const Row &row) const;
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnIdle(wxIdleEvent &event);
  void OnSpinnerTimer(wxTimerEvent &event);

  wxDataViewListCtrl *m_list{nullptr};
  // In the order of the list.
  std::vector<Row> m_rows;
  int m_serial{0};
  std::optional<SessionRef> m_active;
  wxTimer m_spinnerTimer;
  int m_spinnerFrame{0};
};
