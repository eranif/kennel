#include "JobLogViewer.hpp"

#include "core/JobLog.h"

#include <wx/msgdlg.h>

#include <algorithm>

namespace {
wxString EventLabel(const wxString &event) {
  if (event == "start") {
    return _("Start");
  }
  if (event == "end") {
    return _("End");
  }
  if (event == "failed") {
    return _("Failed");
  }
  return event;
}

// Case-insensitive substring match against every visible field, so the
// filter box works the way a log grep would.
bool Matches(const JobLogRecord &r, const wxString &needle) {
  if (needle.empty()) {
    return true;
  }
  const wxString haystack = (r.timestamp + " " + r.event + " " + r.job + " " +
                            r.type + " " + r.trigger + " " + r.session + " " +
                            r.reason + " " + r.message)
                               .Lower();
  return haystack.Contains(needle.Lower());
}
} // namespace

JobLogViewer::JobLogViewer(wxWindow *parent) : JobLogViewerBase(parent) {
  m_dvListCtrlEntries->AppendTextColumn(
      _("Time"), wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlEntries->AppendTextColumn(
      _("Event"), wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlEntries->AppendTextColumn(_("Job"), wxDATAVIEW_CELL_INERT,
                                       wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlEntries->AppendTextColumn(_("Type"), wxDATAVIEW_CELL_INERT,
                                       wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlEntries->AppendTextColumn(_("Trigger"), wxDATAVIEW_CELL_INERT,
                                       wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlEntries->AppendTextColumn(_("Session"), wxDATAVIEW_CELL_INERT,
                                       wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlEntries->AppendTextColumn(_("Message"), wxDATAVIEW_CELL_INERT,
                                       200);

  // Newest first: a log viewer is read top-down for "what just happened".
  m_entries = ReadJobLog();
  std::reverse(m_entries.begin(), m_entries.end());

  PopulateList(wxEmptyString);
  m_searchCtrlFilter->CallAfter(&wxSearchCtrl::SetFocus);
}

JobLogViewer::~JobLogViewer() {}

void JobLogViewer::PopulateList(const wxString &filter) {
  m_dvListCtrlEntries->DeleteAllItems();
  for (const JobLogRecord &r : m_entries) {
    if (!Matches(r, filter)) {
      continue;
    }
    wxVector<wxVariant> cols;
    cols.push_back(r.timestamp);
    cols.push_back(EventLabel(r.event));
    cols.push_back(r.job);
    cols.push_back(r.type);
    cols.push_back(r.trigger);
    cols.push_back(r.session);
    cols.push_back(r.message.empty() ? r.reason : r.message);
    m_dvListCtrlEntries->AppendItem(cols);
  }
}

void JobLogViewer::OnFilterUpdated(wxCommandEvent &event) {
  wxUnusedVar(event);
  PopulateList(m_searchCtrlFilter->GetValue());
}

void JobLogViewer::OnLogEntryActivated(wxDataViewEvent &event) {
  const int row = m_dvListCtrlEntries->ItemToRow(event.GetItem());
  if (row == wxNOT_FOUND || row >= static_cast<int>(m_entries.size())) {
    return;
  }
  // Rows are filtered, so the Nth visible row isn't necessarily m_entries[N];
  // re-resolve by re-matching the same filter up to this row instead of
  // trusting the index directly.
  wxString filter = m_searchCtrlFilter->GetValue();
  int visible = -1;
  for (const JobLogRecord &r : m_entries) {
    if (!Matches(r, filter)) {
      continue;
    }
    if (++visible == row) {
      wxString detail = wxString::Format(
          "%s\n\n%s", r.timestamp,
          r.message.empty() ? r.reason : r.message);
      if (!r.reason.empty() && !r.message.empty()) {
        detail << "\n\n" << _("Reason: ") << r.reason;
      }
      ::wxMessageBox(detail, wxString::Format(_("%s: %s"), EventLabel(r.event),
                                              r.job),
                    wxOK | wxICON_INFORMATION, this);
      return;
    }
  }
}
