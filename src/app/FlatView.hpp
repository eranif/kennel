#pragma once

#include "app/PageViewEvent.hpp"

#include <optional>
#include <vector>
#include <wx/dataview.h>
#include <wx/panel.h>

// Every open page (session, terminal, file) in one list: "icon + name |
// group". It is a projection of what TreeView holds, so it never owns
// anything; MainView feeds it with SetPages(). It keeps no state of its own:
// each row carries its page's name in a hidden column, and the selection is
// the list's selection.
//
// A click on the Name or Group header sorts the list by that column (a second
// click turns the order around). The choice is kept in the UI prefs, so it is
// back on the next start. Never sorted, the list shows the tree order.
//
// Sends wxEVT_PAGEVIEW_SELECTED (through EventNotifier) / wxEVT_PAGEVIEW_MENU
// like TreeView does, and follows the selection made anywhere else.
class FlatView : public wxPanel {
public:
  explicit FlatView(wxWindow *parent);
  ~FlatView() override;

  // Replaces the rows with `pages`, in the given order (unless the list is
  // sorted by a column), and selects `selected` (if given and present).
  // Never sends events.
  void SetPages(const std::vector<PageInfo> &pages,
                const std::optional<SessionRef> &selected);

  void Clear();
  size_t GetItemCount() const { return m_list->GetItemCount(); }

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
  std::optional<SessionRef> RefOf(const wxDataViewItem &item) const;

  wxDataViewListCtrl *m_list{nullptr};
};
