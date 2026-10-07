#pragma once

#include "app/PageViewEvent.hpp"
#include "core/Workspace.h"

#include <optional>
#include <vector>
#include <wx/dataview.h>
#include <wx/panel.h>

// Every open page (session, terminal, file) in one list, most recently used
// first: "icon + name | group". It is a projection of what TreeView holds, so
// it never owns anything; MainView feeds it with SetPages().
//
// Sends wxEVT_PAGEVIEW_SELECTED / wxEVT_PAGEVIEW_MENU like TreeView does.
class FlatView : public wxPanel {
public:
  explicit FlatView(wxWindow *parent);

  // Marks `page` as the most recently used one. False if it already was.
  bool Touch(wxWindow *page);

  // `pages` sorted by recency: `first` (if given) first, then the touched
  // pages, most recent first, then the rest in the order they came in.
  std::vector<PageInfo> Order(const std::vector<PageInfo> &pages,
                              wxWindow *first = nullptr) const;

  // Replaces the rows with `pages` (as given: pass Order()'s result) and
  // selects the one for `current`. Never sends events. Forgets touched pages
  // that are no longer in `pages`.
  void SetPages(const std::vector<PageInfo> &pages, wxWindow *current);

  void Clear();

private:
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnPageSelected(PageViewEvent &event);
  void OnContextMenu(wxDataViewEvent &event);
  void SendMenuEvent(wxWindow *page);
  std::optional<wxDataViewItem> FindByNameAndGroup(const wxString &name,
                                                   const wxString &group);
  std::optional<SessionRef>
  GetNameAndGroupFromItem(const wxDataViewItem &item) const;
  std::optional<SessionRef> GetNameAndGroupFromItem(int row) const;
  wxDataViewListCtrl *m_list{nullptr};
  std::vector<wxWindow *> m_recent; // Most recently touched first
  std::vector<wxWindow *> m_rows;   // The page behind each row
  bool m_updating{false};
};
