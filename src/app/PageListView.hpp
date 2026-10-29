#pragma once

#include "app/PageViewEvent.hpp"

#include "core/Account.h"

#include <optional>
#include <vector>
#include <wx/dataview.h>
#include <wx/panel.h>

// One row of a PageListView.
struct PageRow {
  SessionRef ref;        // Names the page
  wxString label;        // The text of the first column...
  wxBitmapBundle icon;   // ...with its icon
  wxBitmapBundle status; // The status column: the spinner, the check mark, the
                         // hand; not ok for none
  wxString second;       // The text of the second column (the group or a
                         // folder)...
  wxBitmapBundle secondIcon; // ...with its icon (a list of groups only)
  Account account;           // Whose the page is (a file); sent with the
                             // selection of the row
};

// Sent by a PageListView to its parent window (and up) when the user sorted the
// list by a column. GetInt() is the PageListView::SortColumn (kSortNone: not
// sorted any more), GetExtraLong() is 1 for the ascending order.
wxDECLARE_EVENT(wxEVT_PAGELIST_SORTED, wxCommandEvent);

// A list of pages: "icon + name | status | second column". The views of the
// open pages (FlatView, TerminalsView, FilesView) each own one and feed it with
// the rows they chose to show; the list knows nothing about which pages belong
// to which view. After every change of the rows each column but the status is
// fitted to its content, and the list scrolls if they are wider than it. Each
// row carries its page's name in a hidden column. The selected page is also
// kept here: sorting keeps the selected row number, not the selected item (on
// macOS and in the generic control), so the selection is put back after a sort.
//
// Sends wxEVT_PAGEVIEW_SELECTED (through EventNotifier) / wxEVT_PAGEVIEW_MENU
// (to the parent chain; the event object is this list) / wxEVT_PAGELIST_SORTED.
// One page is selected in all the lists together: a list follows the selection
// made anywhere else (it selects the page if it shows it, otherwise it clears
// its own selection).
class PageListView : public wxPanel {
public:
  struct Config {
    wxString secondColumn; // The title of the second column
    bool showStatus{false};
    // The second column shows an icon with its text (the group of a session),
    // not plain text (a folder).
    bool secondIsIconText{false};
    // The status takes the place of the icon of the name (a list without a
    // status column).
    bool statusInName{false};
    // The group all the pages of the list are in (the Terminals or the Files
    // container). Empty: the pages are in various groups, named by the second
    // column.
    wxString group;
  };
  // The columns the list can be sorted by.
  enum SortColumn { kSortNone = -1, kSortName, kSortSecond };

  PageListView(wxWindow *parent, const Config &config);
  ~PageListView() override;

  // Replaces the rows with `rows`, in the given order (unless the list is
  // sorted by a column), and selects `selected` (if given and present). Never
  // sends events.
  void SetRows(const std::vector<PageRow> &rows,
               const std::optional<SessionRef> &selected);

  // Shows `status` (the spinner, the check mark, the hand; not ok for none) on
  // the row of `ref`, without a rebuild of the list. Does nothing when the list
  // does not show the page. `baseIcon` is the icon the page has without a
  // status. Never sends events.
  void SetPageStatus(const SessionRef &ref, const wxBitmapBundle &status,
                     const wxBitmapBundle &baseIcon);

  // Sorts by `column` (kSortNone: no sort, the order of the rows). Never sends
  // events.
  void SetSort(SortColumn column, bool ascending);

  void Clear();
  size_t GetItemCount() const { return m_list->GetItemCount(); }

private:
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnPageSelected(PageViewEvent &event);
  void OnContextMenu(wxDataViewEvent &event);
  void OnColumnSorted(wxDataViewEvent &event);
  // Selects the row of `ref`, without sending events.
  void SelectPage(const SessionRef &ref);
  std::optional<wxDataViewItem> FindRow(const SessionRef &ref) const;
  std::optional<SessionRef> RefOf(const wxDataViewItem &item) const;
  // Fits the width of the columns (all but the status) to their content, once
  // the pending changes are done.
  void FitColumnsSoon();
  void FitColumns();

  Config m_config;
  wxDataViewListCtrl *m_list{nullptr};
  std::optional<SessionRef> m_selected; // The page whose row is selected
  // The pages shown now, in order: when they change, the columns are fitted.
  std::vector<SessionRef> m_rows;
  // The account of each row, in the order the rows were added (the order of
  // the rows of the model, which a sort does not change).
  std::vector<Account> m_accounts;
  bool m_fitPending{false};
};
