#include "app/EditJobsDlg.hpp"
#include "app/JobDlg.hpp"
#include "app/MainFrame.h"

#include "core/AppManager.h"
#include "core/Helpers.h"

#include <wx/button.h>
#include <wx/dataview.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>

#include <algorithm>

namespace {
wxString DescribeSchedule(const JobDef &job) {
  return job.scheduleMode == ScheduleMode::kDailyAt
             ? wxString::Format(_("daily at %02d:%02d"), job.dailyHour,
                                job.dailyMinute)
             : wxString::Format(_("every %dh"), job.intervalHours);
}

// A disabled job's stored next-run time keeps advancing even though the job
// never fires, so showing that time here would be misleading.
wxString DescribeNextRun(const JobDef &job) {
  if (!job.enabled) {
    return "-";
  }
  const wxDateTime nextRun = GetMainFrame()->GetJobScheduler()->NextRunFor(job);
  return nextRun.Format("%Y-%m-%d %H:%M");
}
} // namespace

EditJobsDlg::EditJobsDlg(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _("Manage Jobs"), wxDefaultPosition,
               wxSize(760, 360), wxDEFAULT_DIALOG_STYLE) {
  m_jobs = AppManager::Get().Config().jobs;

  auto *topSizer = new wxBoxSizer(wxVERTICAL);
  auto *rowSizer = new wxBoxSizer(wxHORIZONTAL);

  m_dvListCtrlJobs = new wxDataViewListCtrl(this, wxID_ANY);
  m_dvListCtrlJobs->AppendTextColumn(_("Job"), wxDATAVIEW_CELL_INERT,
                                     wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlJobs->AppendTextColumn(_("Type"), wxDATAVIEW_CELL_INERT,
                                     wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlJobs->AppendTextColumn(_("Schedule"), wxDATAVIEW_CELL_INERT,
                                     wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlJobs->AppendTextColumn(_("Next Run"), wxDATAVIEW_CELL_INERT,
                                     wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlJobs->AppendTextColumn(_("Terminal"), wxDATAVIEW_CELL_INERT,
                                     wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlJobs->AppendTextColumn(_("Enabled"), wxDATAVIEW_CELL_INERT,
                                     wxCOL_WIDTH_AUTOSIZE);
  m_dvListCtrlJobs->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,
                         &EditJobsDlg::OnItemActivated, this);
  rowSizer->Add(m_dvListCtrlJobs, 1, wxEXPAND | wxALL, 10);

  auto *btnColumn = new wxBoxSizer(wxVERTICAL);
  auto *newBtn = new wxButton(this, wxID_ANY, _("New..."));
  auto *editBtn = new wxButton(this, wxID_ANY, _("Edit..."));
  auto *deleteBtn = new wxButton(this, wxID_ANY, _("Delete"));
  auto *runNowBtn = new wxButton(this, wxID_ANY, _("Run Now"));
  newBtn->Bind(wxEVT_BUTTON, &EditJobsDlg::OnNewJob, this);
  editBtn->Bind(wxEVT_BUTTON, &EditJobsDlg::OnEditJob, this);
  deleteBtn->Bind(wxEVT_BUTTON, &EditJobsDlg::OnDeleteJob, this);
  runNowBtn->Bind(wxEVT_BUTTON, &EditJobsDlg::OnRunNow, this);
  editBtn->Bind(wxEVT_UPDATE_UI, &EditJobsDlg::OnEditUI, this);
  deleteBtn->Bind(wxEVT_UPDATE_UI, &EditJobsDlg::OnDeleteUI, this);
  runNowBtn->Bind(wxEVT_UPDATE_UI, &EditJobsDlg::OnRunNowUI, this);
  btnColumn->Add(newBtn, 0, wxEXPAND | wxBOTTOM, 5);
  btnColumn->Add(editBtn, 0, wxEXPAND | wxBOTTOM, 5);
  btnColumn->Add(deleteBtn, 0, wxEXPAND | wxBOTTOM, 5);
  btnColumn->Add(runNowBtn, 0, wxEXPAND);
  rowSizer->Add(btnColumn, 0, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, 10);

  topSizer->Add(rowSizer, 1, wxEXPAND);
  topSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL),
                wxSizerFlags().Border(wxALL, 10).CenterHorizontal());
  SetSizer(topSizer);

  FindWindow(wxID_CANCEL)->Bind(wxEVT_BUTTON, &EditJobsDlg::OnCancel, this);
  Bind(wxEVT_CLOSE_WINDOW, &EditJobsDlg::OnClose, this);
  // CHAR_HOOK (rather than relying on Escape being emulated as a click on
  // wxID_CANCEL) catches Escape regardless of which child control — the
  // job list, a button — currently has focus.
  Bind(wxEVT_CHAR_HOOK, &EditJobsDlg::OnCharHook, this);

  RefreshList();
  m_dvListCtrlJobs->SetFocus();
  ::PositionDialog(this, Orientation::kResize);
}

void EditJobsDlg::RefreshList(int selectRow) {
  m_dvListCtrlJobs->DeleteAllItems();
  for (const JobDef &job : m_jobs) {
    wxVector<wxVariant> cols;
    cols.push_back(job.name);
    cols.push_back(job.type == JobType::kPrompt ? _("Prompt") : _("Command"));
    cols.push_back(DescribeSchedule(job));
    cols.push_back(DescribeNextRun(job));
    cols.push_back(job.keepTerminalOpen ? _("Keeps open") : _("Auto-closes"));
    cols.push_back(job.enabled ? _("Yes") : _("No"));
    m_dvListCtrlJobs->AppendItem(cols);
  }
  if (!m_jobs.empty()) {
    m_dvListCtrlJobs->SelectRow(
        std::clamp(selectRow, 0, static_cast<int>(m_jobs.size()) - 1));
  }
}

int EditJobsDlg::SelectedRow() const {
  return m_dvListCtrlJobs->GetSelectedRow();
}

void EditJobsDlg::OnNewJob(wxCommandEvent &event) {
  wxUnusedVar(event);
  JobDlg dlg{this, nullptr};
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  JobDef job = dlg.GetData();
  for (const JobDef &existing : m_jobs) {
    if (existing.name == job.name) {
      ::wxMessageBox(_("A job with this name already exists"), "Kennel",
                     wxICON_WARNING | wxOK, this);
      return;
    }
  }
  m_jobs.push_back(job);
  m_dirty = true;
  RefreshList(static_cast<int>(m_jobs.size()) - 1);
}

void EditJobsDlg::EditSelection() {
  const int row = SelectedRow();
  if (row == wxNOT_FOUND) {
    return;
  }
  JobDlg dlg{this, &m_jobs[row]};
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  JobDef job = dlg.GetData();
  for (size_t i = 0; i < m_jobs.size(); ++i) {
    if (static_cast<int>(i) != row && m_jobs[i].name == job.name) {
      ::wxMessageBox(_("A job with this name already exists"), "Kennel",
                     wxICON_WARNING | wxOK, this);
      return;
    }
  }
  m_jobs[row] = job;
  m_dirty = true;
  RefreshList(row);
}

void EditJobsDlg::OnEditJob(wxCommandEvent &event) {
  wxUnusedVar(event);
  EditSelection();
}

void EditJobsDlg::OnItemActivated(wxDataViewEvent &event) {
  wxUnusedVar(event);
  EditSelection();
}

void EditJobsDlg::OnDeleteJob(wxCommandEvent &event) {
  wxUnusedVar(event);
  const int row = SelectedRow();
  if (row == wxNOT_FOUND) {
    return;
  }
  if (::wxMessageBox(wxString::Format(_("Delete job '%s'?"), m_jobs[row].name),
                     "Kennel", wxICON_QUESTION | wxYES_NO, this) != wxYES) {
    return;
  }
  m_jobs.erase(m_jobs.begin() + row);
  m_dirty = true;
  RefreshList(row);
}

void EditJobsDlg::OnRunNow(wxCommandEvent &event) {
  wxUnusedVar(event);
  const int row = SelectedRow();
  if (row == wxNOT_FOUND) {
    return;
  }
  GetMainFrame()->GetMainView()->RunJob(m_jobs[row],
                                        /*selectAfterLaunch=*/true);
  // Close as if OK was pressed so the caller (MainFrame::OnManageJobs)
  // persists m_jobs — otherwise any pending New/Edit/Delete made in this
  // session would be silently discarded along with the dialog.
  EndModal(wxID_OK);
}

bool EditJobsDlg::ConfirmDiscardChanges() {
  if (!m_dirty) {
    return true;
  }
  return ::wxMessageBox(_("You have unsaved changes to your jobs. Discard "
                          "them?"),
                        "Kennel", wxICON_WARNING | wxYES_NO, this) == wxYES;
}

void EditJobsDlg::OnCancel(wxCommandEvent &event) {
  wxUnusedVar(event);
  if (!ConfirmDiscardChanges()) {
    return;
  }
  EndModal(wxID_CANCEL);
}

void EditJobsDlg::OnClose(wxCloseEvent &event) {
  if (!ConfirmDiscardChanges()) {
    if (event.CanVeto()) {
      event.Veto();
    }
    return;
  }
  EndModal(wxID_CANCEL);
}

void EditJobsDlg::OnCharHook(wxKeyEvent &event) {
  if (event.GetKeyCode() != WXK_ESCAPE) {
    event.Skip();
    return;
  }
  if (ConfirmDiscardChanges()) {
    EndModal(wxID_CANCEL);
  }
  // Swallow either way: don't let Escape fall through to default handling
  // (which could bypass the dirty-changes prompt).
}

void EditJobsDlg::OnEditUI(wxUpdateUIEvent &event) {
  event.Enable(SelectedRow() != wxNOT_FOUND);
}

void EditJobsDlg::OnDeleteUI(wxUpdateUIEvent &event) {
  event.Enable(SelectedRow() != wxNOT_FOUND);
}

void EditJobsDlg::OnRunNowUI(wxUpdateUIEvent &event) {
  event.Enable(SelectedRow() != wxNOT_FOUND);
}
