#include "app/PageListView.hpp"

#include "core/EventNotifier.hpp"

#include <wx/event.h>
#include <wx/sizer.h>

#include <utility>

wxDEFINE_EVENT(wxEVT_PAGELIST_SORTED, wxCommandEvent);

namespace {
// The model columns. The key (hidden) holds the name of the page.
enum Column { kColumnName = 0, kColumnStatus, kColumnSecond, kColumnKey };

// The width of the status column, in DIP: room for one 16 pixel icon.
constexpr int kStatusWidthDip = 28;

// The text of a cell that holds an icon with its text.
wxString TextOfIconText(const wxVariant &value) {
  wxDataViewIconText iconText;
  iconText << value;
  return iconText.GetText();
}

// The list's model. The control sorts the rows itself (on every platform) and
// asks Compare() for the order; the default Compare() cannot read the
// icon-and-text of the Name column. Without a sort column the rows stay in
// the order they were added: the group order.
class PageListStore : public wxDataViewListStore {
public:
  // `secondIsIconText`: the second column shows an icon with its text (the
  // group), not plain text.
  explicit PageListStore(bool secondIsIconText)
      : m_secondIsIconText(secondIsIconText) {}

  int Compare(const wxDataViewItem &item1, const wxDataViewItem &item2,
              unsigned int column, bool ascending) const override {
    if (column != kColumnName && column != kColumnSecond) {
      return wxDataViewListStore::Compare(item1, item2, column, ascending);
    }
    const unsigned row1 = GetRow(item1);
    const unsigned row2 = GetRow(item2);
    // The other column breaks a tie, and the key a tie on both (two files
    // can have the same label), so the order never depends on the platform.
    const unsigned other = column == kColumnName ? kColumnSecond : kColumnName;
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
    if (column == kColumnName ||
        (column == kColumnSecond && m_secondIsIconText)) {
      return TextOfIconText(value);
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

  bool m_secondIsIconText;
};

// A list of "icon + name | status | second column", plus the hidden key
// column. The status column (an icon, no text) has a fixed width: it is the one
// column that is not fitted to its content (see PageListView::FitColumns()). It
// is hidden when the list has no status column.
wxDataViewListCtrl *NewList(wxWindow *parent, const wxString &secondColumn,
                            bool showStatus, bool secondIsIconText) {
  auto *list =
      new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxDV_ROW_LINES | wxDV_VERT_RULES | wxDV_SINGLE);
  // Before the columns: they are added to the model too.
  auto *store = new PageListStore(secondIsIconText);
  list->AssociateModel(store);
  store->DecRef();

  StylePageView(list);
  list->AppendIconTextColumn(_("Name"), wxDATAVIEW_CELL_INERT,
                             wxCOL_WIDTH_AUTOSIZE)
      ->SetSortable(true);
  // No title, not resizable (flags 0), and centred: only an icon.
  auto *status = list->AppendIconTextColumn(
      wxEmptyString, wxDATAVIEW_CELL_INERT, parent->FromDIP(kStatusWidthDip),
      wxALIGN_CENTER, 0);
  status->SetHidden(!showStatus);
  if (secondIsIconText) {
    list->AppendIconTextColumn(secondColumn, wxDATAVIEW_CELL_INERT,
                               wxCOL_WIDTH_AUTOSIZE)
        ->SetSortable(true);
  } else {
    list->AppendTextColumn(secondColumn, wxDATAVIEW_CELL_INERT,
                           wxCOL_WIDTH_AUTOSIZE)
        ->SetSortable(true);
  }
  // The page's name (session name or file key): the label is not unique.
  list->AppendTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT)->SetHidden(true);
  return list;
}
} // namespace

PageListView::PageListView(wxWindow *parent, const Config &config)
    : wxPanel(parent), m_config(config) {
  m_list = NewList(this, config.secondColumn, config.showStatus,
                   config.secondIsIconText);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  m_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED,
               &PageListView::OnSelectionChanged, this);
  m_list->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &PageListView::OnContextMenu,
               this);
  m_list->Bind(wxEVT_DATAVIEW_COLUMN_SORTED, &PageListView::OnColumnSorted,
               this);
  EventNotifier::Get()->Bind(wxEVT_PAGEVIEW_SELECTED,
                             &PageListView::OnPageSelected, this);
}

PageListView::~PageListView() {
  EventNotifier::Get()->Unbind(wxEVT_PAGEVIEW_SELECTED,
                               &PageListView::OnPageSelected, this);
}

void PageListView::SetRows(const std::vector<PageRow> &rows,
                           const std::optional<SessionRef> &selected) {
  // Rebuilding (and selecting below) must not look like a user's pick. On
  // macOS a programmatic selection does send the event.
  wxEventBlocker blocker(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_list->DeleteAllItems();
  m_selected.reset();
  std::vector<SessionRef> refs;
  for (const PageRow &page : rows) {
    refs.push_back(page.ref);
    wxVector<wxVariant> row;
    row.push_back(wxVariant(wxDataViewIconText(page.label, page.icon)));
    row.push_back(wxVariant(wxDataViewIconText(wxEmptyString, page.status)));
    if (m_config.secondIsIconText) {
      row.push_back(
          wxVariant(wxDataViewIconText(page.second, page.secondIcon)));
    } else {
      row.push_back(wxVariant(page.second));
    }
    row.push_back(wxVariant(page.ref.name));
    m_list->AppendItem(row);
  }
  if (selected) {
    SelectPage(*selected);
  }
  // Only a page that came or went (or was renamed or moved) changes the widths
  // the columns need. A refresh that leaves the rows as they are (a new
  // directory, say) keeps the widths, so a width the user dragged stays.
  if (refs != m_rows) {
    m_rows = std::move(refs);
    FitColumnsSoon();
  }
}

void PageListView::SetPageStatus(const SessionRef &ref,
                                 const wxBitmapBundle &status,
                                 const wxBitmapBundle &baseIcon) {
  auto item = FindRow(ref);
  if (!item) {
    return;
  }
  const int row = m_list->ItemToRow(*item);
  // On macOS a change of a value can send a selection event.
  wxEventBlocker blocker(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  if (m_config.statusInName) {
    wxVariant value;
    m_list->GetValue(value, row, kColumnName);
    wxDataViewIconText iconText;
    iconText << value;
    iconText.SetBitmapBundle(status.IsOk() ? status : baseIcon);
    m_list->SetValue(wxVariant(iconText), row, kColumnName);
    return;
  }
  // An icon that is not ok empties the cell. The column has a fixed width, so
  // nothing needs to be fitted.
  m_list->SetValue(wxVariant(wxDataViewIconText(wxEmptyString, status)), row,
                   kColumnStatus);
}

void PageListView::SetSort(SortColumn column, bool ascending) {
  if (column == kSortName) {
    m_list->GetColumn(kColumnName)->SetSortOrder(ascending);
  } else if (column == kSortSecond) {
    m_list->GetColumn(kColumnSecond)->SetSortOrder(ascending);
  }
}

void PageListView::Clear() {
  wxEventBlocker blocker(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_list->DeleteAllItems();
  m_selected.reset();
  m_rows.clear();
  FitColumnsSoon();
}

void PageListView::FitColumnsSoon() {
  // Deferred: one pass serves a burst of changes, and the control has taken the
  // new rows into account by then.
  if (m_fitPending) {
    return;
  }
  m_fitPending = true;
  CallAfter(&PageListView::FitColumns);
}

void PageListView::FitColumns() {
  m_fitPending = false;
  for (unsigned i = 0; i < m_list->GetColumnCount(); ++i) {
    auto *column = m_list->GetColumn(i);
    // The status column keeps its width, and the key is hidden.
    if (column->GetModelColumn() == kColumnStatus ||
        column->GetModelColumn() == kColumnKey) {
      continue;
    }
    // A column that is autosized is as wide as its widest cell. The control may
    // have fixed the width since (it stretches the last column to the free
    // space, and the user can drag a border), so ask for the autosize again.
    // A list that is wider than the view scrolls.
    column->SetWidth(wxCOL_WIDTH_AUTOSIZE);
  }
}

void PageListView::SelectPage(const SessionRef &ref) {
  auto item = FindRow(ref);
  if (!item) {
    return;
  }
  m_selected = ref;
  wxEventBlocker blocker(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  if (m_list->GetSelection() != *item) {
    m_list->Select(*item);
  }
  m_list->EnsureVisible(*item);
}

void PageListView::OnPageSelected(PageViewEvent &event) {
  event.Skip();
  if (event.GetEventObject() == this) {
    return;
  }
  const SessionRef ref = event.GetRef();
  if (FindRow(ref)) {
    SelectPage(ref);
    return;
  }
  // One page is selected in all the lists: the page is shown elsewhere.
  wxEventBlocker blocker(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_list->UnselectAll();
  m_selected.reset();
}

void PageListView::OnSelectionChanged(wxDataViewEvent &event) {
  auto ref = RefOf(event.GetItem());
  if (!ref) {
    return;
  }
  m_selected = ref;
  // A page picked here (mouse or keyboard) is the most recently used one:
  // Ctrl+Tab goes back to the page shown before it. The other lists clear their
  // selection when they get the event.
  PageViewEvent evtSelected(wxEVT_PAGEVIEW_SELECTED);
  evtSelected.SetEventObject(this);
  evtSelected.SetUpdateRecent(true);
  evtSelected.SetRef(*ref);
  EventNotifier::Get()->AddPendingEvent(evtSelected);
}

void PageListView::OnContextMenu(wxDataViewEvent &event) {
  PageViewEvent menu(wxEVT_PAGEVIEW_MENU);
  menu.SetEventObject(this);
  if (auto ref = RefOf(event.GetItem())) {
    menu.SetRef(*ref);
  }
  ProcessWindowEvent(menu);
}

void PageListView::OnColumnSorted(wxDataViewEvent &event) {
  event.Skip();
  // After the sort the same row number is selected, which now holds another
  // page (on macOS this event comes before the rows move). Select the page
  // again, and keep it in sight: it may have moved far away.
  CallAfter([this] {
    if (m_selected) {
      SelectPage(*m_selected);
    }
  });

  const wxDataViewColumn *column = event.GetDataViewColumn();
  if (column == nullptr) {
    column = m_list->GetSortingColumn();
  }
  SortColumn sorted = kSortNone;
  bool ascending = true;
  if (column != nullptr && column->IsSortKey()) {
    if (column->GetModelColumn() == kColumnName) {
      sorted = kSortName;
    } else if (column->GetModelColumn() == kColumnSecond) {
      sorted = kSortSecond;
    }
    ascending = column->IsSortOrderAscending();
  }
  wxCommandEvent sortEvent(wxEVT_PAGELIST_SORTED);
  sortEvent.SetEventObject(this);
  sortEvent.SetInt(sorted);
  sortEvent.SetExtraLong(ascending ? 1 : 0);
  ProcessWindowEvent(sortEvent);
}

std::optional<wxDataViewItem>
PageListView::FindRow(const SessionRef &ref) const {
  const int count = static_cast<int>(m_list->GetItemCount());
  for (int row = 0; row < count; ++row) {
    auto item = m_list->RowToItem(row);
    if (RefOf(item) == ref) {
      return item;
    }
  }
  return std::nullopt;
}

std::optional<SessionRef>
PageListView::RefOf(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return std::nullopt;
  }
  const int row = m_list->ItemToRow(item);
  if (row < 0 || row >= static_cast<int>(m_list->GetItemCount())) {
    return std::nullopt;
  }
  wxString group = m_config.group;
  if (group.empty()) {
    // The pages are in various groups: the second column names it.
    wxVariant value;
    m_list->GetValue(value, row, kColumnSecond);
    group = TextOfIconText(value);
  }
  return SessionRef{group, m_list->GetTextValue(row, kColumnKey)};
}
