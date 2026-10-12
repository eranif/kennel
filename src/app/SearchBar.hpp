#pragma once

#include <wx/containr.h>
#include <wx/control.h>

class wxButton;
class wxCheckBox;
class wxTextCtrl;

// Sent (it goes up to the parent) when the text or an option changes, and on
// Enter / Shift+Enter in the find text. The string of the event is the text to
// look for, its int the SearchBar::Flags of the search.
wxDECLARE_EVENT(wxEVT_SEARCHBAR_FIND, wxCommandEvent);
// Replace / Replace all (the button, or Enter in the replace text for
// Replace). The string and the int are as for wxEVT_SEARCHBAR_FIND; the
// replacement is GetReplaceText().
wxDECLARE_EVENT(wxEVT_SEARCHBAR_REPLACE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SEARCHBAR_REPLACE_ALL, wxCommandEvent);
// Sent on Escape: the owner hides the bar and gives the focus back.
wxDECLARE_EVENT(wxEVT_SEARCHBAR_CLOSE, wxCommandEvent);

// A search bar for a page:
//   [ find what    ][ Down    ][ Up          ][x Full word][x Case sensitive]
//   [ replace with ][ Replace ][ Replace all ]
// A terminal has the first line without the options (kFindOnly); a file has
// both lines (kEditor). It searches as the user types (kIncremental); Enter in
// the find text looks for the next match, Shift+Enter for the previous one (as
// the Down and Up buttons do). The owner does the search and the replace: the
// bar only sends events.
class SearchBar : public wxNavigationEnabled<wxControl> {
public:
  enum Flags {
    kForward = 1 << 0,
    // The text or an option changed: the match may stay where it is.
    kIncremental = 1 << 1,
    kWholeWord = 1 << 2,
    kMatchCase = 1 << 3,
  };
  enum class Kind { kFindOnly, kEditor };

  SearchBar(wxWindow *parent, Kind kind);

  // Gives the focus to the find text and selects it. A non-empty `text`
  // replaces the text first (no event is sent for it).
  void Activate(const wxString &text);
  wxString GetText() const;
  wxString GetReplaceText() const;
  // kWholeWord and kMatchCase, as the checkboxes are.
  int GetOptions() const;
  void SetOptions(int options);
  // A read-only file: the replace line is there but cannot be used.
  void EnableReplace(bool enable);

private:
  void Send(wxEventType type, int flags);
  void OnCharHook(wxKeyEvent &event);
  // Makes the buttons of a file's bar as wide as "Replace all".
  void UpdateButtonWidths();
  // Whether the focus is in `ctrl`.
  static bool HasFocusIn(const wxWindow *ctrl);

  wxTextCtrl *m_find{nullptr};
  wxButton *m_next{nullptr};     // Only set in a file's bar
  wxButton *m_previous{nullptr}; // Only set in a file's bar
  wxCheckBox *m_wholeWord{nullptr};
  wxCheckBox *m_matchCase{nullptr};
  wxTextCtrl *m_replace{nullptr};
  wxButton *m_replaceButton{nullptr};
  wxButton *m_replaceAllButton{nullptr};
};
