#pragma once

#include <vector>
#include <wx/bmpbndl.h>
#include <wx/event.h>
#include <wx/eventfilter.h>
#include <wx/popupwin.h>
#include <wx/timer.h>

class wxDataViewEvent;
class wxDataViewListCtrl;

// One row of the switcher.
struct PageSwitcherItem {
  wxString label;
  wxString dir;   // The folder of the page (empty for a file)
  wxString group; // The group, or the Terminals / Files container
  wxBitmapBundle icon;
};

// Sent to the sink when the switcher is done. GetInt() is the chosen row, or
// wxNOT_FOUND if cancelled (Escape, or a click outside of the popup). The
// switcher destroys itself right after the event was queued: the receiver must
// forget it, and must not destroy it.
wxDECLARE_EVENT(wxEVT_PAGE_SWITCHER_DONE, wxCommandEvent);

// A small "task switcher" popup (think Ctrl+Tab in an IDE): it lists the open
// pages, the caller keeps Ctrl held down and presses Tab / Shift+Tab to move
// the selection, and releasing Ctrl accepts it. Escape cancels.
//
// It is a wxPopupTransientWindow, so it does not take the activation from the
// frame. Create it with `new`: it shows itself and destroys itself when done.
class PageSwitcherDlg : public wxPopupTransientWindow, public wxEventFilter {
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

  // wxEventFilter: sees the keys of the whole application. The popup does not
  // have the keyboard focus, so it gets no key events of its own.
  int FilterEvent(wxEvent &event) override;

protected:
  // Called after the popup was hidden: by Finish(), or by a click outside.
  void OnDismiss() override;

private:
  void Finish(bool accept);
  void OnTimer(wxTimerEvent &event);
  void OnActivated(wxDataViewEvent &event);

  wxDataViewListCtrl *m_list{nullptr};
  wxEvtHandler *m_sink{nullptr};
  int m_result{wxNOT_FOUND};
  bool m_done{false};
  wxTimer m_timer;
};
