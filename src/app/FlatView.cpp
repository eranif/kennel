#include "app/FlatView.hpp"

#include "core/EventNotifier.hpp"

#include <wx/event.h>
#include <wx/sizer.h>

namespace {
enum Column { kColumnName = 0, kColumnGroup, kColumnKey };
} // namespace

FlatView::FlatView(wxWindow *parent) : wxPanel(parent) {
  m_list = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  StylePageView(m_list);
  m_list->AppendIconTextColumn(_("Name"), wxDATAVIEW_CELL_INERT, 160);
  m_list->AppendTextColumn(_("Group"), wxDATAVIEW_CELL_INERT, 100);
  // The page's name (session name or file key): the label is not unique.
  m_list->AppendTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT)
      ->SetHidden(true);

  m_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &FlatView::OnSelectionChanged,
               this);
  m_list->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &FlatView::OnContextMenu,
               this);
  EventNotifier::Get()->Bind(wxEVT_PAGEVIEW_SELECTED, &FlatView::OnPageSelected,
                             this);
}

FlatView::~FlatView() {
  EventNotifier::Get()->Unbind(wxEVT_PAGEVIEW_SELECTED,
                               &FlatView::OnPageSelected, this);
}

void FlatView::SetPages(const std::vector<PageInfo> &pages,
                        const std::optional<GroupAndName> &selected) {
  // Rebuilding (and selecting below) must not look like a user's pick. On
  // macOS a programmatic selection does send the event.
  wxEventBlocker blocker(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_list->DeleteAllItems();
  for (const auto &info : pages) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(wxDataViewIconText(info.label, info.icon)));
    row.push_back(wxVariant(info.group));
    row.push_back(wxVariant(info.key));
    m_list->AppendItem(row);
  }
  if (selected) {
    SelectPage(*selected);
  }
}

void FlatView::Clear() {
  wxEventBlocker blocker(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_list->DeleteAllItems();
}

void FlatView::SelectPage(const GroupAndName &ref) {
  auto item = FindRow(ref);
  if (!item || m_list->GetSelection() == *item) {
    return;
  }
  wxEventBlocker blocker(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_list->Select(*item);
  m_list->EnsureVisible(*item);
}

void FlatView::OnPageSelected(PageViewEvent &event) {
  event.Skip();
  if (event.GetEventObject() == this) {
    return;
  }
  SelectPage(event.GetRef());
}

void FlatView::OnSelectionChanged(wxDataViewEvent &event) {
  auto ref = RefOf(event.GetItem());
  if (!ref) {
    return;
  }
  // Picking a page here (mouse or keyboard) does not change the Ctrl+Tab
  // order.
  PageViewEvent evtSelected(wxEVT_PAGEVIEW_SELECTED);
  evtSelected.SetEventObject(this);
  evtSelected.SetUpdateRecent(false);
  evtSelected.SetRef(*ref);
  EventNotifier::Get()->AddPendingEvent(evtSelected);
}

void FlatView::OnContextMenu(wxDataViewEvent &event) {
  PageViewEvent menu(wxEVT_PAGEVIEW_MENU);
  menu.SetEventObject(this);
  if (auto ref = RefOf(event.GetItem())) {
    menu.SetRef(*ref);
  }
  ProcessWindowEvent(menu);
}

std::optional<wxDataViewItem> FlatView::FindRow(const GroupAndName &ref) const {
  const int count = static_cast<int>(m_list->GetItemCount());
  for (int row = 0; row < count; ++row) {
    auto item = m_list->RowToItem(row);
    if (RefOf(item) == ref) {
      return item;
    }
  }
  return std::nullopt;
}

std::optional<GroupAndName> FlatView::RefOf(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return std::nullopt;
  }
  const int row = m_list->ItemToRow(item);
  if (row < 0 || row >= static_cast<int>(m_list->GetItemCount())) {
    return std::nullopt;
  }
  return GroupAndName{m_list->GetTextValue(row, kColumnKey),
                      m_list->GetTextValue(row, kColumnGroup)};
}
