#include "app/FlatView.hpp"
#include "core/EventNotifier.hpp"
#include "wx/app.h"

#include <wx/sizer.h>

#include <algorithm>

FlatView::FlatView(wxWindow *parent) : wxPanel(parent) {
  m_list = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  StylePageView(m_list);
  m_list->AppendIconTextColumn(_("Name"), wxDATAVIEW_CELL_INERT, 160);
  m_list->AppendTextColumn(_("Group"), wxDATAVIEW_CELL_INERT, 100);

  m_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &FlatView::OnSelectionChanged,
               this);
  m_list->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &FlatView::OnContextMenu,
               this);

  EventNotifier::Get()->Bind(wxEVT_PAGEVIEW_SELECTED, &FlatView::OnPageSelected,
                             this);
}

bool FlatView::Touch(wxWindow *page) {
  if (!m_recent.empty() && m_recent.front() == page) {
    return false;
  }
  m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), page),
                 m_recent.end());
  m_recent.insert(m_recent.begin(), page);
  return true;
}

std::vector<PageInfo> FlatView::Order(const std::vector<PageInfo> &pages,
                                      wxWindow *first) const {
  std::vector<PageInfo> remaining = pages;
  std::vector<PageInfo> ordered;
  auto take = [&](wxWindow *page) {
    auto it = std::find_if(
        remaining.begin(), remaining.end(),
        [page](const PageInfo &info) { return info.page == page; });
    if (it != remaining.end()) {
      ordered.push_back(std::move(*it));
      remaining.erase(it);
    }
  };

  if (first != nullptr) {
    take(first);
  }
  for (auto *page : m_recent) {
    take(page);
  }
  ordered.insert(ordered.end(), std::make_move_iterator(remaining.begin()),
                 std::make_move_iterator(remaining.end()));
  return ordered;
}

void FlatView::SetPages(const std::vector<PageInfo> &pages, wxWindow *current) {
  // Changing the list from inside its own selection handler is asking for
  // trouble, and the selection set below must not look like a user's click.
  m_updating = true;
  m_list->DeleteAllItems();
  m_rows.clear();

  int currentRow = wxNOT_FOUND;
  for (const auto &info : pages) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(wxDataViewIconText(info.name, info.icon)));
    row.push_back(wxVariant(info.group));
    m_list->AppendItem(row);
    if (info.page == current) {
      currentRow = static_cast<int>(m_rows.size());
    }
    m_rows.push_back(info.page);
  }
  if (currentRow != wxNOT_FOUND) {
    m_list->SelectRow(static_cast<unsigned int>(currentRow));
  }

  m_recent.erase(std::remove_if(m_recent.begin(), m_recent.end(),
                                [&](wxWindow *page) {
                                  return std::find(m_rows.begin(), m_rows.end(),
                                                   page) == m_rows.end();
                                }),
                 m_recent.end());
  m_updating = false;
}

void FlatView::Clear() {
  m_updating = true;
  m_list->DeleteAllItems();
  m_rows.clear();
  m_recent.clear();
  m_updating = false;
}

void FlatView::OnPageSelected(PageViewEvent &event) {
  event.Skip();
  if (event.GetEventObject() == this) {
    return;
  }

  // Update our selection
  auto result =
      FindByNameAndGroup(event.GetSessionName(), event.GetGroupName());
  if (!result)
    return;
  m_list->Select(*result);
}

void FlatView::OnSelectionChanged(wxDataViewEvent &event) {
  if (m_updating) {
    return;
  }
  const int row = m_list->ItemToRow(event.GetItem());
  if (row < 0 || row >= static_cast<int>(m_rows.size())) {
    return;
  }

  PageViewEvent evtSelected(wxEVT_PAGEVIEW_SELECTED);
  evtSelected.SetEventObject(this);
  evtSelected.SetUpdateRecent(true);
  evtSelected.SetSessionName(m_list->GetTextValue(row, 0));
  evtSelected.SetGroupName(m_list->GetTextValue(row, 1));
  EventNotifier::Get()->AddPendingEvent(evtSelected);
}

std::optional<wxDataViewItem>
FlatView::FindByNameAndGroup(const wxString &name, const wxString &group) {
  for (auto row = 0; row < m_list->GetItemCount(); ++row) {
    auto result = GetNameAndGroupFromItem(row);
    if (!result)
      continue;
    if (result->name == name && result->groupName == group)
      return m_list->RowToItem(row);
  }
  return std::nullopt;
}

void FlatView::OnContextMenu(wxDataViewEvent &event) {
  const int row = m_list->ItemToRow(event.GetItem());
  SendMenuEvent(row >= 0 && row < static_cast<int>(m_rows.size()) ? m_rows[row]
                                                                  : nullptr);
}

void FlatView::SendMenuEvent(wxWindow *page) {
  PageViewEvent menu(wxEVT_PAGEVIEW_MENU);
  menu.SetEventObject(this);
  ProcessWindowEvent(menu);
}

std::optional<SessionRef>
FlatView::GetNameAndGroupFromItem(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return std::nullopt;
  }
  return GetNameAndGroupFromItem(m_list->ItemToRow(item));
}

std::optional<SessionRef> FlatView::GetNameAndGroupFromItem(int row) const {
  if (m_list->GetItemCount() == 0 || row < 0 || row >= m_list->GetItemCount())
    return std::nullopt;

  return SessionRef{
      .groupName = m_list->GetTextValue(row, 1),
      .name = m_list->GetTextValue(row, 0),
  };
}
