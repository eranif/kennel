#pragma once

#include "app/ReviewBuddyEvent.hpp"

#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

class wxTerminalViewCtrl;

// Shows the state of a ReviewBuddy in the header of a terminal (the main
// agent's) while the review is open: a spinner while somebody is working, then
// the status text. The final status has a mark in front: a thumbs up when
// the review is clean, a thumbs down when it failed, a red circle when the
// round limit was reached. It replaces what the header showed before (see
// SessionPage::UpdateHeader()); destroy it to give the header back.
//
// It follows the events of the ReviewBuddy (see ReviewBuddyEvent.hpp). They
// are sent to the owner of the ReviewBuddy (`source`): the header binds to it,
// and the owner's own handlers must call Skip() so that both get the events.
// The terminal must outlive the header, or the header must be destroyed first.
class ReviewBuddyHeader : public wxEvtHandler {
public:
  ReviewBuddyHeader(wxTerminalViewCtrl *terminal, wxEvtHandler *source);
  ~ReviewBuddyHeader() override;

  // Shows `text` in the header of `terminal`: yellow on a darker strip on a
  // dark theme, the colours of the theme otherwise. Does nothing when the
  // header shows this text already, unless `force`.
  static void SetText(wxTerminalViewCtrl *terminal, const wxString &text,
                      bool force = false);

  // Draws the header again (the theme changed).
  void Refresh() { Render(/*force=*/true); }

private:
  void OnReviewEvent(ReviewBuddyEvent &event);
  void OnTimer(wxTimerEvent &);
  void Render(bool force = false);

  wxTerminalViewCtrl *m_terminal;
  wxEvtHandler *m_source;
  wxTimer m_timer;
  size_t m_frame{0};
  wxString m_text; // The status text, without the spinner
  wxString m_mark; // In front of the final status: clean, or needs attention
  bool m_spinning{false}; // Somebody is working
};
