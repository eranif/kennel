#pragma once

#include "core/Account.h"

#include <wx/string.h>

// Where Find in Files starts for a page: the machine and the folder.
struct SearchTarget {
  Account account;
  wxString folder;
};

// What a page of the main notebook (a session, a file) has to offer for Find:
// the search bar of the page, and what Find in Files starts from. A page that
// can be searched derives from this next to its window class, and
// MainNotebook::GetSearchablePage() gives the one that is showing.
class SearchablePage {
public:
  virtual ~SearchablePage() = default;

  // Whether the page has a search at all (for the menu items).
  virtual bool CanFind() const = 0;
  // Shows the search bar of the page. The selected text (see GetFindWhat()) is
  // what it looks for.
  virtual void ShowFind() = 0;
  // Whether there is a search to repeat with FindNext().
  virtual bool CanFindNext() const = 0;
  // Selects the next (or previous) match of the last search. Shows the search
  // bar if there was no search yet.
  virtual void FindNext(bool forward) = 0;
  // The text that is selected in the page (its first line); empty when nothing
  // is. What Find and Find in Files look for.
  virtual wxString GetFindWhat() const = 0;
  // The machine and the folder that Find in Files searches in, from this page.
  virtual SearchTarget GetSearchTarget() const = 0;
};
