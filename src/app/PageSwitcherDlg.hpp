#pragma once

#include <vector>
#include <wx/dialog.h>
#include <wx/icon.h>
#include <wx/timer.h>

class wxDataViewEvent;
class wxDataViewListCtrl;

// One row of the switcher.
struct PageSwitcherItem {
  wxString label;
  wxIcon icon;
};

// A small "task switcher" popup (think Ctrl+Tab in an IDE): it lists the open
// pages, the caller keeps Ctrl held down and presses Tab / Shift+Tab to move
// the selection, and releasing Ctrl accepts it. Escape cancels.
class PageSwitcherDlg : public wxDialog {
public:
  // `items` are the rows, current page first. The initial selection is the
  // row after the first one (forward) or the last one (backward).
  PageSwitcherDlg(wxWindow *parent, const std::vector<PageSwitcherItem> &items,
                  bool forward);
  ~PageSwitcherDlg() override;

  // The chosen row, or wxNOT_FOUND.
  int GetSelectedIndex() const;

private:
  void Advance(bool forward);
  void OnCharHook(wxKeyEvent &event);
  void OnKeyUp(wxKeyEvent &event);
  void OnTimer(wxTimerEvent &event);
  void OnActivated(wxDataViewEvent &event);

  wxDataViewListCtrl *m_list{nullptr};
  wxTimer m_timer;
};
