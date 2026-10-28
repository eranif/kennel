#include "app/TerminalsView.hpp"

#include "app/PageRegistry.hpp"

#include <wx/sizer.h>

TerminalsView::TerminalsView(wxWindow *parent) : wxPanel(parent) {
  m_list = new PageListView(this, {.secondColumn = _("Folder"),
                                   .statusInName = true,
                                   .group = kTerminalsGroupName});
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);
}

void TerminalsView::SetPages(const std::vector<PageInfo> &pages,
                             const std::optional<SessionRef> &selected) {
  std::vector<PageRow> rows;
  for (const PageInfo &info : pages) {
    if (info.group != kTerminalsGroupName) {
      continue;
    }
    rows.push_back({.ref = info.Ref(),
                    .label = info.label,
                    .icon = info.DisplayIcon(),
                    .second = info.dir});
  }
  m_list->SetRows(rows, selected);
}

void TerminalsView::SetPageStatusIcon(const SessionRef &ref,
                                      const wxBitmapBundle &status,
                                      const wxBitmapBundle &baseIcon) {
  m_list->SetPageStatus(ref, status, baseIcon);
}

void TerminalsView::Clear() { m_list->Clear(); }
