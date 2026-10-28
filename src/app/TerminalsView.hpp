#pragma once

#include "app/PageListView.hpp"

#include <optional>
#include <vector>

// The plain terminals: "icon + name | folder". A terminal has no status
// column: the status (the spinner, the check mark or the hand) takes the place
// of the icon of the name. MainView feeds it with SetPages().
// A click on a header sorts the list; the choice is not kept.
class TerminalsView : public wxPanel {
public:
  explicit TerminalsView(wxWindow *parent);

  // Shows the terminals among `pages`, in the given order, and selects
  // `selected` (if given and present). Never sends events.
  void SetPages(const std::vector<PageInfo> &pages,
                const std::optional<SessionRef> &selected);
  // Shows `status` (the spinner, the check mark, the hand; not ok for none) in
  // place of the icon of the row of `ref`, which gets `baseIcon` back without a
  // status. Never sends events.
  void SetPageStatusIcon(const SessionRef &ref, const wxBitmapBundle &status,
                         const wxBitmapBundle &baseIcon);
  void Clear();
  size_t GetItemCount() const { return m_list->GetItemCount(); }

private:
  PageListView *m_list{nullptr};
};
