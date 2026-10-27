#pragma once

#include "app/PageViewEvent.hpp"

#include <optional>
#include <vector>
#include <wx/dataview.h>
#include <wx/panel.h>

class wxSplitterWindow;

// Every open page (session, terminal, file) in one list: "icon + name |
// group". It is a projection of what PageRegistry holds, so it never owns
// anything; MainView feeds it with SetPages(). Each row carries its page's name
// in a hidden column. The selected page is also kept here: sorting keeps the
// selected row number, not the selected item (on macOS and in the generic
// control), so the selection is put back after a sort.
//
// A click on the Name or Group header sorts the list by that column (a second
// click turns the order around). The choice is kept in the UI prefs, so it is
// back on the next start. Never sorted, the list shows the group order.
//
// Sends wxEVT_PAGEVIEW_SELECTED (through EventNotifier) / wxEVT_PAGEVIEW_MENU
// and follows the selection made anywhere else.
class FlatView : public wxPanel {
public:
  explicit FlatView(wxWindow *parent);
  ~FlatView() override;

  // Replaces the rows with `pages`, in the given order (unless the list is
  // sorted by a column), and selects `selected` (if given and present).
  // Never sends events.
  void SetPages(const std::vector<PageInfo> &pages,
                const std::optional<SessionRef> &selected);

  // Gives the row of `ref` another icon (the spinner, the check mark, ...),
  // without a rebuild of the list. Never sends events.
  void SetPageIcon(const SessionRef &ref, const wxBitmapBundle &icon);

  void Clear();

  // The height of the upper list (the sessions and files), in DIP. Needs a
  // list that has its final size to take effect.
  int GetSashPosition() const;
  void SetSashPosition(int dip);
  bool HasSize() const;
  size_t GetItemCount() const {
    return m_list->GetItemCount() + m_terminalList->GetItemCount();
  }

private:
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnPageSelected(PageViewEvent &event);
  void OnContextMenu(wxDataViewEvent &event);
  // The user sorted by a column: remember it.
  void OnColumnSorted(wxDataViewEvent &event);
  // Sorts by the column saved in the UI prefs, if any.
  void RestoreSortOrder();
  // Selects the row of `ref`, without sending events.
  void SelectPage(const SessionRef &ref);
  std::optional<wxDataViewItem> FindRow(const SessionRef &ref) const;
  static bool IsTerminalGroup(const wxString &group);
  // The list that shows the pages of `group`.
  wxDataViewListCtrl *ListFor(const wxString &group) const;
  std::optional<SessionRef> RefOf(wxDataViewListCtrl *list,
                                  const wxDataViewItem &item) const;

  wxSplitterWindow *m_splitter{nullptr};
  wxDataViewListCtrl *m_list{nullptr};
  wxDataViewListCtrl *m_terminalList{nullptr};
  std::optional<SessionRef> m_selected; // The page whose row is selected
};
