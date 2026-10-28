#pragma once

#include <wx/event.h>
#include <wx/panel.h>

class wxTextCtrl;

// Sent (it goes up to the parent) when the user types in the bar. The string of
// the event is the whole text of the box, prefix included. It is not sent for
// the text that SetInitialText() puts in.
wxDECLARE_EVENT(wxEVT_GOTOANYTHING_CHANGED, wxCommandEvent);
// Sent when the bar closes itself. The string is the text of the box, the int
// is the GotoAnythingBar::CloseReason. The bar is hidden already.
wxDECLARE_EVENT(wxEVT_GOTOANYTHING_CLOSED, wxCommandEvent);

// A box on top of the page, as the one of Sublime Text: one line of text that
// starts with a prefix which says what the user is asking for (":" is a line of
// the file, later "@" a symbol, no prefix a file...). The bar knows nothing of
// what the prefix means: it shows the text it is given, tells the
// owner what is typed (so the owner can show the result as the user types) and
// how it was closed, and that is all. The owner does the rest.
//
// The owner puts it in a sizer (it is hidden to begin with), shows it and gives
// it the text with SetInitialText(); the bar hides itself when it closes, and
// the owner lays its parent out again.
//   Enter   closes it, kAccepted
//   Escape  closes it, kCancelled: the owner puts back what it moved
//   a click elsewhere (the focus leaves the box) closes it, kFocusLost: the
//           owner leaves things as they are
class GotoAnythingBar : public wxPanel {
public:
  enum CloseReason { kAccepted, kCancelled, kFocusLost };

  explicit GotoAnythingBar(wxWindow *parent);

  // Puts `text` in the box, the caret at its end, and gives the focus to the
  // box. The owner has shown the bar. No event is sent for the text.
  void SetInitialText(const wxString &text);
  wxString GetText() const;

private:
  // Hides the bar and tells the owner.
  void CloseWith(CloseReason reason);
  void OnText(wxCommandEvent &event);
  void OnEnter(wxCommandEvent &event);
  void OnCharHook(wxKeyEvent &event);
  void OnKillFocus(wxFocusEvent &event);

  wxTextCtrl *m_text{nullptr};
};
