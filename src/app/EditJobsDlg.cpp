#include "app/EditJobsDlg.hpp"
#include "app/JobDlg.hpp"
#include "app/MainFrame.h"

#include "core/AppManager.h"
#include "core/Helpers.h"

#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>

#include <algorithm>

namespace {
wxString DescribeJob(const JobDef &job) {
  wxString label = wxString::Format(
      "%s  (%s, every %dh, %s)", job.name,
      job.type == JobType::kPrompt ? _("Prompt") : _("Command"),
      job.intervalHours,
      job.keepTerminalOpen ? _("keeps terminal open") : _("auto-closes"));
  if (!job.enabled) {
    label << "  [" << _("disabled") << "]";
  }
  return label;
}
} // namespace

EditJobsDlg::EditJobsDlg(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _("Manage Jobs"), wxDefaultPosition,
               wxSize(560, 360), wxDEFAULT_DIALOG_STYLE) {
  m_jobs = AppManager::Get().Config().jobs;

  auto *topSizer = new wxBoxSizer(wxVERTICAL);
  auto *rowSizer = new wxBoxSizer(wxHORIZONTAL);

  m_listBoxJobs = new wxListBox(this, wxID_ANY);
  m_listBoxJobs->Bind(wxEVT_LISTBOX_DCLICK, &EditJobsDlg::OnListDClick, this);
  rowSizer->Add(m_listBoxJobs, 1, wxEXPAND | wxALL, 10);

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

  // Escape emulates a click on wxID_CANCEL, so this one bind covers the
  // Cancel button, Escape, and (together with the close-event bind below)
  // the title bar's X.
  FindWindow(wxID_CANCEL)->Bind(wxEVT_BUTTON, &EditJobsDlg::OnCancel, this);
  Bind(wxEVT_CLOSE_WINDOW, &EditJobsDlg::OnClose, this);

  RefreshList();
  m_listBoxJobs->SetFocus();
  ::PositionDialog(this, Orientation::kResize);
}

void EditJobsDlg::RefreshList(int selectRow) {
  m_listBoxJobs->Clear();
  for (const JobDef &job : m_jobs) {
    m_listBoxJobs->Append(DescribeJob(job));
  }
  if (!m_jobs.empty()) {
    m_listBoxJobs->SetSelection(
        std::clamp(selectRow, 0, static_cast<int>(m_jobs.size()) - 1));
  }
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
  const int row = m_listBoxJobs->GetSelection();
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

void EditJobsDlg::OnListDClick(wxCommandEvent &event) {
  wxUnusedVar(event);
  EditSelection();
}

void EditJobsDlg::OnDeleteJob(wxCommandEvent &event) {
  wxUnusedVar(event);
  const int row = m_listBoxJobs->GetSelection();
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
  const int row = m_listBoxJobs->GetSelection();
  if (row == wxNOT_FOUND) {
    return;
  }
  GetMainFrame()->GetMainView()->RunJob(m_jobs[row], /*selectAfterLaunch=*/true);
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

void EditJobsDlg::OnEditUI(wxUpdateUIEvent &event) {
  event.Enable(m_listBoxJobs->GetSelection() != wxNOT_FOUND);
}

void EditJobsDlg::OnDeleteUI(wxUpdateUIEvent &event) {
  event.Enable(m_listBoxJobs->GetSelection() != wxNOT_FOUND);
}

void EditJobsDlg::OnRunNowUI(wxUpdateUIEvent &event) {
  event.Enable(m_listBoxJobs->GetSelection() != wxNOT_FOUND);
}
