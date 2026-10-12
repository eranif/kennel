#pragma once

#include <wx/containr.h>
#include <wx/control.h>

class wxCheckBox;
class wxSearchCtrl;

// Sent (it goes up to the parent) when the text or an option changes, and on
// Enter / Shift+Enter in the search control. The string of the event is the
// text to look for, its int the SearchBar::Flags of the search.
wxDECLARE_EVENT(wxEVT_SEARCHBAR_FIND, wxCommandEvent);
// Sent on Escape: the owner hides the bar and gives the focus back.
wxDECLARE_EVENT(wxEVT_SEARCHBAR_CLOSE, wxCommandEvent);

// A search bar for a page:
//   "[ search control ][Up][Down][x Full word][x Case sensitive]".
// The options are there only if the bar is created with them. It searches as
// the user types (kIncremental); Enter looks for the next match, Shift+Enter
// for the previous one (as the Down and Up buttons do). The owner does
// the search: the bar only sends events.
class SearchBar : public wxNavigationEnabled<wxControl> {
public:
  enum Flags {
    kForward = 1 << 0,
    // The text or an option changed: the match may stay where it is.
    kIncremental = 1 << 1,
    kWholeWord = 1 << 2,
    kMatchCase = 1 << 3,
  };

  SearchBar(wxWindow *parent, bool withOptions);

  // Gives the focus to the search control and selects its text. A non-empty
  // `text` replaces the text first (no event is sent for it).
  void Activate(const wxString &text);
  wxString GetText() const;
  // kWholeWord and kMatchCase, as the checkboxes are.
  int GetOptions() const;
  void SetOptions(int options);

private:
  void Send(int flags);
  void OnCharHook(wxKeyEvent &event);

  wxSearchCtrl *m_search{nullptr};
  wxCheckBox *m_wholeWord{nullptr};
  wxCheckBox *m_matchCase{nullptr};
};
