#pragma once

#include "app/PageViewEvent.hpp"

#include <optional>
#include <vector>
#include <wx/dataview.h>
#include <wx/panel.h>

class wxSplitterWindow;

// Every open page (session, terminal, file) in two lists. The sessions and the
// files: "icon + name | status | icon + group", where the status is the
// spinner, the check mark or the hand (an icon in a column of its own, always
// the same width). The terminals: "icon + name | directory" (the status
// replaces the icon of the name). After every change of the rows each column
// but the status is fitted to its content. It is a projection of what
// PageRegistry holds, so it never owns anything; MainView feeds it with
// SetPages(). Each row carries its page's name in a hidden column. The selected
// page is also kept here: sorting keeps the selected row number, not the
// selected item (on macOS and in the generic control), so the selection is put
// back after a sort.
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

  // Shows `status` (the spinner, the check mark, the hand; not ok for none) on
  // the row of `ref`, without a rebuild of the list. A terminal has no status
  // column: its row shows `status` in place of `baseIcon`. Never sends events.
  void SetPageStatusIcon(const SessionRef &ref, const wxBitmapBundle &status,
                         const wxBitmapBundle &baseIcon);

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
  // Fits the width of the columns (all but the status) to their content, once
  // the pending changes are done.
  void FitColumnsSoon();
  void FitColumns();
  // A list got a new size: fits again if the last fit had no size to go by.
  void OnListSize(wxSizeEvent &event);

  wxSplitterWindow *m_splitter{nullptr};
  wxDataViewListCtrl *m_list{nullptr};
  wxDataViewListCtrl *m_terminalList{nullptr};
  std::optional<SessionRef> m_selected; // The page whose row is selected
  // The pages shown now, in order: when they change, the columns are fitted.
  std::vector<SessionRef> m_rows;
  bool m_fitPending{false};
  // The last fit ran before the lists had a size, so it could not cap the
  // width.
  bool m_fitSizeUnknown{false};
};
