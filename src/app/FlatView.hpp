#pragma once

#include "app/PageListView.hpp"

#include <optional>
#include <vector>

// The agent sessions: "icon + name | status | icon + group", where the status
// is the spinner, the check mark or the hand (an icon in a column of its own,
// always the same width). The terminals and the files have a view of their own
// (TerminalsView, FilesView). MainView feeds it with SetPages().
//
// A click on the Name or Group header sorts the list by that column (a second
// click turns the order around). The choice is kept in the UI prefs, so it is
// back on the next start. Never sorted, the list shows the group order.
class FlatView : public wxPanel {
public:
  explicit FlatView(wxWindow *parent);

  // Shows the sessions among `pages`, in the given order (unless the list is
  // sorted by a column), and selects `selected` (if given and present). Never
  // sends events.
  void SetPages(const std::vector<PageInfo> &pages,
                const std::optional<SessionRef> &selected);
  // Shows `status` (the spinner, the check mark, the hand; not ok for none) on
  // the row of `ref`. Never sends events.
  void SetPageStatusIcon(const SessionRef &ref, const wxBitmapBundle &status,
                         const wxBitmapBundle &baseIcon);
  void Clear();
  size_t GetItemCount() const { return m_list->GetItemCount(); }

private:
  // Sorts by the column saved in the UI prefs, if any.
  void RestoreSortOrder();
  // The user sorted the list: remember it.
  void OnSorted(wxCommandEvent &event);

  PageListView *m_list{nullptr};
};
