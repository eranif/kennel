#include "app/FlatView.hpp"

#include "app/PageRegistry.hpp"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Logger.h"

#include <wx/event.h>
#include <wx/sizer.h>
#include <wx/splitter.h>

#include <utility>

namespace {
// The model columns of both lists. The list of the sessions shows all of them
// but the key: "icon + name | status | icon + group". The list of the
// terminals shows "icon + name | directory" (its status column is hidden, and
// its group column holds the directory).
enum Column { kColumnName = 0, kColumnStatus, kColumnGroup, kColumnKey };

// How a sorted column is named in the UI prefs.
constexpr const char *kSortByName = "name";
constexpr const char *kSortByGroup = "group";

// The width of the status column, in DIP: room for one 16 pixel icon.
constexpr int kStatusWidthDip = 28;
// A fitted column is never narrower than this when it is capped, in DIP.
constexpr int kMinCapDip = 60;

// The text of a cell that holds an icon with its text.
wxString IconTextOf(const wxVariant &value) {
  wxDataViewIconText iconText;
  iconText << value;
  return iconText.GetText();
}

// The list's model. The control sorts the rows itself (on every platform) and
// asks Compare() for the order; the default Compare() cannot read the
// icon-and-text of the Name column. Without a sort column the rows stay in
// the order they were added: the group order.
class FlatViewStore : public wxDataViewListStore {
public:
  // `secondIsIconText`: the second column shows an icon with its text (the
  // group), not plain text.
  explicit FlatViewStore(bool secondIsIconText)
      : m_secondIsIconText(secondIsIconText) {}

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
    if (column == kColumnName ||
        (column == kColumnGroup && m_secondIsIconText)) {
      return IconTextOf(value);
    }
    return value.GetString();
  }

  bool m_secondIsIconText;

  // Ignores case first, so "beta" comes before "Gamma".
  int CompareText(unsigned row1, unsigned row2, unsigned column) const {
    const wxString text1 = TextOf(row1, column);
    const wxString text2 = TextOf(row2, column);
    const int result = text1.CmpNoCase(text2);
    return result != 0 ? result : text1.Cmp(text2);
  }
};

// A list of "icon + name | status | second column", plus the hidden key
// column. The status column (an icon, no text) has a fixed width: it is the one
// column that is not fitted to its content (see FlatView::FitColumns()). It is
// hidden when the list has no status column. The second column has an icon too
// when `secondIsIconText` (the group), and is plain text otherwise (the
// directory).
wxDataViewListCtrl *NewList(wxWindow *parent, const wxString &secondColumn,
                            bool showStatus, bool secondIsIconText) {
  auto *list =
      new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxDV_ROW_LINES | wxDV_SINGLE);
  // Before the columns: they are added to the model too.
  auto *store = new FlatViewStore(secondIsIconText);
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

FlatView::FlatView(wxWindow *parent) : wxPanel(parent) {
  // The splitter fills the panel; the two lists are its panes (the splitter
  // sizes them, so they are not in a sizer).
  m_splitter =
      new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxSP_LIVE_UPDATE | wxSP_3DSASH);
  m_splitter->SetSashGravity(0.5);
  m_splitter->SetMinimumPaneSize(FromDIP(60));
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_splitter, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  // The sessions and the files on top, the terminals below.
  m_list = NewList(m_splitter, _("Group"), /*showStatus=*/true,
                   /*secondIsIconText=*/true);
  m_terminalList = NewList(m_splitter, _("Directory"), /*showStatus=*/false,
                           /*secondIsIconText=*/false);
  m_splitter->SplitHorizontally(m_list, m_terminalList);
  RestoreSortOrder();

  for (auto *list : {m_list, m_terminalList}) {
    list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &FlatView::OnSelectionChanged,
               this);
    list->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &FlatView::OnContextMenu,
               this);
    list->Bind(wxEVT_SIZE, &FlatView::OnListSize, this);
  }
  // Bound after RestoreSortOrder(): on macOS restoring sends this event too.
  // Only the top list is sorted (and the sort saved): the terminals keep the
  // group order.
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
}

void FlatView::SetPages(const std::vector<PageInfo> &pages,
                        const std::optional<SessionRef> &selected) {
  // Rebuilding (and selecting below) must not look like a user's pick. On
  // macOS a programmatic selection does send the event.
  wxEventBlocker blocker1(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  wxEventBlocker blocker2(m_terminalList, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_list->DeleteAllItems();
  m_terminalList->DeleteAllItems();
  m_selected = selected;
  std::vector<SessionRef> rows;
  for (const auto &info : pages) {
    rows.push_back(info.Ref());
    const bool terminal = IsTerminalGroup(info.group);
    wxVector<wxVariant> row;
    // A terminal has no status column: its icon shows the status instead.
    row.push_back(wxVariant(wxDataViewIconText(
        info.label, terminal ? info.DisplayIcon() : info.icon)));
    row.push_back(wxVariant(wxDataViewIconText(
        wxEmptyString, terminal ? wxBitmapBundle{} : info.statusIcon)));
    if (terminal) {
      row.push_back(wxVariant(info.dir));
    } else {
      row.push_back(wxVariant(wxDataViewIconText(info.group, info.groupIcon)));
    }
    row.push_back(wxVariant(info.key));
    ListFor(info.group)->AppendItem(row);
  }
  if (selected) {
    SelectPage(*selected);
  }
  // Only a page that came or went (or was renamed or moved) changes the widths
  // the columns need. A refresh that leaves the rows as they are (a new
  // directory, say) keeps the widths, so a width the user dragged stays.
  if (rows != m_rows) {
    m_rows = std::move(rows);
    FitColumnsSoon();
  }
}

void FlatView::SetPageStatusIcon(const SessionRef &ref,
                                 const wxBitmapBundle &status,
                                 const wxBitmapBundle &baseIcon) {
  auto item = FindRow(ref);
  if (!item) {
    return;
  }
  auto *list = ListFor(ref.groupName);
  const int row = list->ItemToRow(*item);
  // On macOS a change of a value can send a selection event.
  wxEventBlocker blocker(list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  if (list == m_terminalList) {
    // No status column: the status takes the place of the icon of the name.
    wxVariant value;
    list->GetValue(value, row, kColumnName);
    wxDataViewIconText iconText;
    iconText << value;
    iconText.SetBitmapBundle(status.IsOk() ? status : baseIcon);
    list->SetValue(wxVariant(iconText), row, kColumnName);
    return;
  }
  // An icon that is not ok empties the cell. The column has a fixed width, so
  // nothing needs to be fitted.
  list->SetValue(wxVariant(wxDataViewIconText(wxEmptyString, status)), row,
                 kColumnStatus);
}

void FlatView::Clear() {
  wxEventBlocker blocker1(m_list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  wxEventBlocker blocker2(m_terminalList, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_list->DeleteAllItems();
  m_terminalList->DeleteAllItems();
  m_selected.reset();
  m_rows.clear();
  FitColumnsSoon();
}

void FlatView::FitColumnsSoon() {
  // Deferred: the lists have their final size by then (at start-up the rows are
  // added before the window is laid out), and one pass serves a burst of
  // changes.
  if (m_fitPending) {
    return;
  }
  m_fitPending = true;
  CallAfter(&FlatView::FitColumns);
}

void FlatView::FitColumns() {
  m_fitPending = false;
  m_fitSizeUnknown = false;
  for (auto *list : {m_list, m_terminalList}) {
    // One long name or directory must not push the other column out of view:
    // each fitted column gets at most half of the width that the status column
    // (when it is shown) leaves. A list that is not laid out yet has a size
    // that means nothing: no cap then, and OnListSize() fits again once the
    // list has its size.
    const bool statusShown = !list->GetColumn(kColumnStatus)->IsHidden();
    const int status = statusShown ? list->FromDIP(kStatusWidthDip) : 0;
    const int width = list->GetClientSize().x;
    int cap = 0;
    if (width >= status + 2 * list->FromDIP(kMinCapDip)) {
      cap = (width - status) / 2;
    } else {
      m_fitSizeUnknown = true;
    }
    for (unsigned i = 0; i < list->GetColumnCount(); ++i) {
      auto *column = list->GetColumn(i);
      // The status column keeps its width, and the key is hidden.
      if (column->GetModelColumn() == kColumnStatus ||
          column->GetModelColumn() == kColumnKey) {
        continue;
      }
      // The generic control keeps an autosized column fitted by itself; the
      // native ones (macOS, GTK) fit it once, when this is called.
      column->SetWidth(wxCOL_WIDTH_AUTOSIZE);
      if (cap > 0 && column->GetWidth() > cap) {
        column->SetWidth(cap);
      }
    }
  }
}

void FlatView::OnListSize(wxSizeEvent &event) {
  event.Skip();
  // Only for a fit that had no size to go by. Later resizes of the window do
  // not touch the widths, so a column the user dragged stays.
  if (m_fitSizeUnknown) {
    FitColumnsSoon();
  }
}

bool FlatView::IsTerminalGroup(const wxString &group) {
  return group == kTerminalsGroupName;
}

wxDataViewListCtrl *FlatView::ListFor(const wxString &group) const {
  return IsTerminalGroup(group) ? m_terminalList : m_list;
}

void FlatView::SelectPage(const SessionRef &ref) {
  m_selected = ref;
  auto item = FindRow(ref);
  if (!item) {
    return;
  }
  auto *list = ListFor(ref.groupName);
  auto *other = list == m_list ? m_terminalList : m_list;
  // One row is selected in the whole view.
  wxEventBlocker blocker1(list, wxEVT_DATAVIEW_SELECTION_CHANGED);
  wxEventBlocker blocker2(other, wxEVT_DATAVIEW_SELECTION_CHANGED);
  other->UnselectAll();
  if (list->GetSelection() != *item) {
    list->Select(*item);
  }
  list->EnsureVisible(*item);
}

void FlatView::OnPageSelected(PageViewEvent &event) {
  event.Skip();
  if (event.GetEventObject() == this) {
    return;
  }
  SelectPage(event.GetRef());
}

void FlatView::OnSelectionChanged(wxDataViewEvent &event) {
  auto *list = dynamic_cast<wxDataViewListCtrl *>(event.GetEventObject());
  if (list == nullptr) {
    return;
  }
  auto ref = RefOf(list, event.GetItem());
  if (!ref) {
    return;
  }
  // One row is selected in the whole view.
  auto *other = list == m_list ? m_terminalList : m_list;
  {
    wxEventBlocker blocker(other, wxEVT_DATAVIEW_SELECTION_CHANGED);
    other->UnselectAll();
  }
  m_selected = ref;
  // A page picked here (mouse or keyboard) is the most recently used one:
  // Ctrl+Tab goes back to the page shown before it.
  PageViewEvent evtSelected(wxEVT_PAGEVIEW_SELECTED);
  evtSelected.SetEventObject(this);
  evtSelected.SetUpdateRecent(true);
  evtSelected.SetRef(*ref);
  EventNotifier::Get()->AddPendingEvent(evtSelected);
}

void FlatView::OnContextMenu(wxDataViewEvent &event) {
  PageViewEvent menu(wxEVT_PAGEVIEW_MENU);
  menu.SetEventObject(this);
  auto *list = dynamic_cast<wxDataViewListCtrl *>(event.GetEventObject());
  if (list != nullptr) {
    if (auto ref = RefOf(list, event.GetItem())) {
      menu.SetRef(*ref);
    }
  }
  ProcessWindowEvent(menu);
}

std::optional<wxDataViewItem> FlatView::FindRow(const SessionRef &ref) const {
  auto *list = ListFor(ref.groupName);
  const int count = static_cast<int>(list->GetItemCount());
  for (int row = 0; row < count; ++row) {
    auto item = list->RowToItem(row);
    if (RefOf(list, item) == ref) {
      return item;
    }
  }
  return std::nullopt;
}

std::optional<SessionRef> FlatView::RefOf(wxDataViewListCtrl *list,
                                          const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return std::nullopt;
  }
  const int row = list->ItemToRow(item);
  if (row < 0 || row >= static_cast<int>(list->GetItemCount())) {
    return std::nullopt;
  }
  // The terminals have no group column: they all are in the same group.
  wxString group = kTerminalsGroupName;
  if (list != m_terminalList) {
    wxVariant value;
    list->GetValue(value, row, kColumnGroup);
    group = IconTextOf(value);
  }
  return SessionRef{group, list->GetTextValue(row, kColumnKey)};
}

int FlatView::GetSashPosition() const {
  return m_splitter->ToDIP(m_splitter->GetSashPosition());
}

void FlatView::SetSashPosition(int dip) {
  if (dip > 0 && m_splitter->IsSplit()) {
    m_splitter->SetSashPosition(m_splitter->FromDIP(dip));
  }
}

bool FlatView::HasSize() const { return m_splitter->GetSize().y > 0; }
