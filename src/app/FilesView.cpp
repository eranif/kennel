#include "app/FilesView.hpp"

#include "app/PageRegistry.hpp"

#include <wx/sizer.h>

FilesView::FilesView(wxWindow *parent) : wxPanel(parent) {
  m_list = new PageListView(
      this, {.secondColumn = _("Folder"), .group = kFilesGroupName});
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);
}

void FilesView::SetPages(const std::vector<PageInfo> &pages,
                         const std::optional<SessionRef> &selected) {
  std::vector<PageRow> rows;
  for (const PageInfo &info : pages) {
    if (info.group != kFilesGroupName) {
      continue;
    }
    rows.push_back({.ref = info.Ref(),
                    .label = info.label,
                    .icon = info.icon,
                    .second = info.dir});
  }
  m_list->SetRows(rows, selected);
}

void FilesView::Clear() { m_list->Clear(); }
