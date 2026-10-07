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
// Sends wxEVT_PAGEVIEW_SELECTED (through EventNotifier) / wxEVT_PAGEVIEW_MENU
// like TreeView does, and follows the selection made anywhere else.
class FlatView : public wxPanel {
public:
  explicit FlatView(wxWindow *parent);
  ~FlatView() override;

  // Replaces the rows with `pages`, in the given order, and selects
  // `selected` (if given and present). Never sends events.
  void SetPages(const std::vector<PageInfo> &pages,
                const std::optional<GroupAndName> &selected);

  void Clear();

private:
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnPageSelected(PageViewEvent &event);
  void OnContextMenu(wxDataViewEvent &event);
  // Selects the row of `ref`, without sending events.
  void SelectPage(const GroupAndName &ref);
  std::optional<wxDataViewItem> FindRow(const GroupAndName &ref) const;
  std::optional<GroupAndName> RefOf(const wxDataViewItem &item) const;

  wxDataViewListCtrl *m_list{nullptr};
};
