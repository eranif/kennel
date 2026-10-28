#pragma once

#include "app/PageListView.hpp"

#include <optional>
#include <vector>

// The open files: "icon + name | folder". MainView feeds it with SetPages().
// A click on a header sorts the list; the choice is not kept.
class FilesView : public wxPanel {
public:
  explicit FilesView(wxWindow *parent);

  // Shows the files among `pages`, in the given order, and selects `selected`
  // (if given and present). Never sends events.
  void SetPages(const std::vector<PageInfo> &pages,
                const std::optional<SessionRef> &selected);
  void Clear();
  size_t GetItemCount() const { return m_list->GetItemCount(); }

private:
  PageListView *m_list{nullptr};
};
