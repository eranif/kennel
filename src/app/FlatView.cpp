#include "app/FlatView.hpp"

#include "app/PageRegistry.hpp"
#include "core/AppManager.h"
#include "core/Logger.h"

#include <wx/sizer.h>

namespace {
// How a sorted column is named in the UI prefs.
constexpr const char *kSortByName = "name";
constexpr const char *kSortByGroup = "group";
} // namespace

FlatView::FlatView(wxWindow *parent) : wxPanel(parent) {
  m_list = new PageListView(this, {.secondColumn = _("Group"),
                                   .showStatus = true,
                                   .secondIsIconText = true});
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  RestoreSortOrder();
  // Bound after RestoreSortOrder(): on macOS restoring sends the event too.
  Bind(wxEVT_PAGELIST_SORTED, &FlatView::OnSorted, this);
}

void FlatView::SetPages(const std::vector<PageInfo> &pages,
                        const std::optional<SessionRef> &selected) {
  std::vector<PageRow> rows;
  for (const PageInfo &info : pages) {
    if (info.group == kTerminalsGroupName || info.group == kFilesGroupName) {
      continue;
    }
    rows.push_back({.ref = info.Ref(),
                    .label = info.label,
                    .icon = info.icon,
                    .status = info.statusIcon,
                    .second = info.group,
                    .secondIcon = info.groupIcon});
  }
  m_list->SetRows(rows, selected);
}

void FlatView::SetPageStatusIcon(const SessionRef &ref,
                                 const wxBitmapBundle &status,
                                 const wxBitmapBundle &baseIcon) {
  m_list->SetPageStatus(ref, status, baseIcon);
}

void FlatView::Clear() { m_list->Clear(); }

void FlatView::RestoreSortOrder() {
  const auto &prefs = AppManager::Get().GetPrefs();
  if (prefs.flatViewSortColumn == kSortByName) {
    m_list->SetSort(PageListView::kSortName, prefs.flatViewSortAscending);
  } else if (prefs.flatViewSortColumn == kSortByGroup) {
    m_list->SetSort(PageListView::kSortSecond, prefs.flatViewSortAscending);
  }
}

void FlatView::OnSorted(wxCommandEvent &event) {
  wxString name;
  if (event.GetInt() == PageListView::kSortName) {
    name = kSortByName;
  } else if (event.GetInt() == PageListView::kSortSecond) {
    name = kSortByGroup;
  }
  const bool ascending = event.GetExtraLong() != 0;

  auto &prefs = AppManager::Get().GetPrefs();
  if (prefs.flatViewSortColumn == name &&
      prefs.flatViewSortAscending == ascending) {
    return;
  }
  prefs.flatViewSortColumn = name;
  prefs.flatViewSortAscending = ascending;
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not save the sort order of the sessions list: "
                << st.message();
  }
}
