#include "app/FlatView.hpp"

#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Logger.h"

#include <wx/event.h>
#include <wx/sizer.h>

namespace {
enum Column { kColumnName = 0, kColumnGroup, kColumnKey };

// How a sorted column is named in the UI prefs.
constexpr const char *kSortByName = "name";
constexpr const char *kSortByGroup = "group";

// The list's model. The control sorts the rows itself (on every platform) and
// asks Compare() for the order; the default Compare() cannot read the
// icon-and-text of the Name column. Without a sort column the rows stay in
// the order they were added: the tree order.
class FlatViewStore : public wxDataViewListStore {
public:
  int Compare(const wxDataViewItem &item1, const wxDataViewItem &item2,
              unsigned int column, bool ascending) const override {
    if (column != kColumnName && column != kColumnGroup) {
      return wxDataViewListStore::Compare(item1, item2, column, ascending);
    }
    const unsigned row1 = GetRow(item1);
    const unsigned row2 = GetRow(item2);
    // The other column breaks a tie, and the key a tie on both (two files
    // can have the same label), so the order never depends on the platform.
    const unsigned other = column == kColumnName ? kColumnGroup : kColumnName;
    int result = CompareText(row1, row2, column);
    if (result == 0) {
      result = CompareText(row1, row2, other);
    }
    if (result == 0) {
      result = CompareText(row1, row2, kColumnKey);
    }
    return ascending ? result : -result;
  }

private:
  wxString TextOf(unsigned row, unsigned column) const {
    wxVariant value;
    GetValueByRow(value, row, column);
    if (column == kColumnName) {
      wxDataViewIconText iconText;
      iconText << value;
      return iconText.GetText();
    }
    return value.GetString();
  }

  // Ignores case first, so "beta" comes before "Gamma".
  int CompareText(unsigned row1, unsigned row2, unsigned column) const {
    const wxString text1 = TextOf(row1, column);
    const wxString text2 = TextOf(row2, column);
    const int result = text1.CmpNoCase(text2);
    return result != 0 ? result : text1.Cmp(text2);
  }
};
} // namespace

FlatView::FlatView(wxWindow *parent) : wxPanel(parent) {
  m_list = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  // Before the columns: they are added to the model too.
  auto *store = new FlatViewStore;
  m_list->AssociateModel(store);
  store->DecRef();

  StylePageView(m_list);
  m_list->AppendIconTextColumn(_("Name"), wxDATAVIEW_CELL_INERT, 160)
      ->SetSortable(true);
  m_list->AppendTextColumn(_("Group"), wxDATAVIEW_CELL_INERT, 100)
      ->SetSortable(true);
  // The page's name (session name or file key): the label is not unique.
  m_list->AppendTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT)
      ->SetHidden(true);
  RestoreSortOrder();

  m_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &FlatView::OnSelectionChanged,
               this);
  m_list->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &FlatView::OnContextMenu,
               this);
  // Bound after RestoreSortOrder(): on macOS restoring sends this event too.
  m_list->Bind(wxEVT_DATAVIEW_COLUMN_SORTED, &FlatView::OnColumnSorted, this);
  EventNotifier::Get()->Bind(wxEVT_PAGEVIEW_SELECTED, &FlatView::OnPageSelected,
                             this);
}

FlatView::~FlatView() {
  EventNotifier::Get()->Unbind(wxEVT_PAGEVIEW_SELECTED,
                               &FlatView::OnPageSelected, this);
}

void FlatView::RestoreSortOrder() {
  const auto &prefs = AppManager::Get().GetPrefs();
  int column = wxNOT_FOUND;
  if (prefs.flatViewSortColumn == kSortByName) {
    column = kColumnName;
  } else if (prefs.flatViewSortColumn == kSortByGroup) {
    column = kColumnGroup;
  }
  if (column != wxNOT_FOUND) {
    m_list->GetColumn(column)->SetSortOrder(prefs.flatViewSortAscending);
  }
}

void FlatView::OnColumnSorted(wxDataViewEvent &event) {
  event.Skip();
  const wxDataViewColumn *column = event.GetDataViewColumn();
  if (column == nullptr) {
    column = m_list->GetSortingColumn();
  }
  wxString name;
  bool ascending = true;
  if (column != nullptr && column->IsSortKey()) {
    const unsigned model = column->GetModelColumn();
    if (model == kColumnName) {
      name = kSortByName;
    } else if (model == kColumnGroup) {
      name = kSortByGroup;
    }
    ascending = column->IsSortOrderAscending();
  }

  auto &prefs = AppManager::Get().GetPrefs();
  if (prefs.flatViewSortColumn == name &&
      prefs.flatViewSortAscending == ascending) {
    return;
  }
  prefs.flatViewSortColumn = name;
  prefs.flatViewSortAscending = ascending;
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not save the flat list sort order: " << st.message();
  }

  // Keep the selected page in sight: it may have moved far away.
  CallAfter([this] {
    const wxDataViewItem selected = m_list->GetSelection();
    if (selected.IsOk()) {
      m_list->EnsureVisible(selected);
    }
  });
}

void FlatView::SetPages(const std::vector<PageInfo> &pages,
                        const std::optional<SessionRef> &selected) {
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

void FlatView::SelectPage(const SessionRef &ref) {
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

std::optional<wxDataViewItem> FlatView::FindRow(const SessionRef &ref) const {
  const int count = static_cast<int>(m_list->GetItemCount());
  for (int row = 0; row < count; ++row) {
    auto item = m_list->RowToItem(row);
    if (RefOf(item) == ref) {
      return item;
    }
  }
  return std::nullopt;
}

std::optional<SessionRef> FlatView::RefOf(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return std::nullopt;
  }
  const int row = m_list->ItemToRow(item);
  if (row < 0 || row >= static_cast<int>(m_list->GetItemCount())) {
    return std::nullopt;
  }
  return SessionRef{m_list->GetTextValue(row, kColumnGroup),
                    m_list->GetTextValue(row, kColumnKey)};
}
