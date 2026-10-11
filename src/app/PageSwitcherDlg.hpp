#pragma once

#include <vector>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/icon.h>
#include <wx/timer.h>

class wxDataViewEvent;
class wxDataViewListCtrl;

// One row of the switcher.
struct PageSwitcherItem {
  wxString label;
  wxIcon icon;
};

// Sent to the sink when the switcher is done. GetInt() is the chosen row, or
// wxNOT_FOUND if cancelled. The switcher is modeless: the receiver must destroy
// it (Destroy()), preferably with CallAfter.
wxDECLARE_EVENT(wxEVT_PAGE_SWITCHER_DONE, wxCommandEvent);

// A small "task switcher" popup (think Ctrl+Tab in an IDE): it lists the open
// pages, the caller keeps Ctrl held down and presses Tab / Shift+Tab to move
// the selection, and releasing Ctrl accepts it. Escape cancels.
class PageSwitcherDlg : public wxDialog {
public:
  // `items` are the rows, current page first. The initial selection is the
  // row after the first one (forward) or the last one (backward).
  PageSwitcherDlg(wxWindow *parent, wxEvtHandler *sink,
                  const std::vector<PageSwitcherItem> &items, bool forward);
  ~PageSwitcherDlg() override;

  // The chosen row, or wxNOT_FOUND.
  int GetSelectedIndex() const;

  // Moves the selection one row.
  void Advance(bool forward);

private:
  void Finish(bool accept);
  void OnCharHook(wxKeyEvent &event);
  void OnKeyUp(wxKeyEvent &event);
  void OnTimer(wxTimerEvent &event);
  void OnActivated(wxDataViewEvent &event);

  wxDataViewListCtrl *m_list{nullptr};
  wxEvtHandler *m_sink{nullptr};
  bool m_done{false};
  wxTimer m_timer;
};
