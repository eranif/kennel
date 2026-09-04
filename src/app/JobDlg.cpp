#include "app/JobDlg.hpp"

#include "core/AppManager.h"
#include "core/Helpers.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
constexpr int kJobTypeRawCommand = 0;
constexpr int kJobTypePrompt = 1;
} // namespace

JobDlg::JobDlg(wxWindow *parent, const JobDef *job)
    : JobDlgBase(parent, wxID_ANY, job ? _("Edit Job") : _("New Job")) {
  BuildUi();
  PopulateAgents();

  if (job) {
    m_textCtrlName->SetValue(job->name);
    m_choiceJobType->SetSelection(
        job->type == JobType::kPrompt ? kJobTypePrompt : kJobTypeRawCommand);
    m_textCtrlCommand->SetValue(job->type == JobType::kPrompt ? job->prompt
                                                              : job->command);
    if (job->type == JobType::kPrompt) {
      m_choiceAgent->SetStringSelection(job->agentName);
    }
    m_spinIntervalHours->SetValue(job->intervalHours);
    m_checkBoxKeepTerminalOpen->SetValue(job->keepTerminalOpen);
    m_checkBoxEnabled->SetValue(job->enabled);
  } else {
    m_choiceJobType->SetSelection(kJobTypeRawCommand);
    m_spinIntervalHours->SetValue(1);
    m_checkBoxKeepTerminalOpen->SetValue(true);
    m_checkBoxEnabled->SetValue(true);
  }

  UpdateFieldsForType();
  GetSizer()->Fit(this);
  ::PositionDialog(this, Orientation::kTop);
}

void JobDlg::BuildUi() {
  m_choiceJobType->Clear();
  m_choiceJobType->Append(_("Raw Command"));
  m_choiceJobType->Append(_("Prompt"));
}

void JobDlg::PopulateAgents() {
  m_choiceAgent->Clear();
  const AdapterRegistry &registry = AppManager::Get().Adapters();
  for (const wxString &name : registry.AgentNames()) {
    const AgentDef *agent = registry.FindAgent(name);
    if (agent && !agent->nonInteractiveArg.empty()) {
      m_choiceAgent->Append(name);
    }
  }
  if (!m_choiceAgent->IsEmpty()) {
    m_choiceAgent->SetSelection(0);
  }
}

void JobDlg::OnJobTypeChanged(wxCommandEvent &event) {
  wxUnusedVar(event);
  UpdateFieldsForType();
}

void JobDlg::UpdateFieldsForType() {
  const bool isPrompt = m_choiceJobType->GetSelection() == kJobTypePrompt;
  m_staticTextAgent->Show(isPrompt);
  m_choiceAgent->Show(isPrompt);
  m_staticTextCommand->SetLabel(isPrompt ? _("Prompt:") : _("Command:"));
  GetSizer()->Layout();
}

wxString JobDlg::Validate() const {
  wxString name = m_textCtrlName->GetValue();
  name.Trim().Trim(false);
  if (name.empty()) {
    return _("Please enter a job name.");
  }
  if (m_textCtrlCommand->GetValue().empty()) {
    return m_choiceJobType->GetSelection() == kJobTypePrompt
               ? _("Please enter a prompt.")
               : _("Please enter a command.");
  }
  if (m_choiceJobType->GetSelection() == kJobTypePrompt &&
      m_choiceAgent->GetSelection() == wxNOT_FOUND) {
    return _("No agent is available for prompt jobs. Configure an agent's "
             "\"Non-Interactive Switch\" first (Settings > Manage Agents).");
  }
  return wxEmptyString;
}

void JobDlg::OnOk(wxCommandEvent &event) {
  wxUnusedVar(event);
  wxString error = Validate();
  if (!error.empty()) {
    ::wxMessageBox(error, "Kennel", wxOK | wxICON_WARNING, this);
    return;
  }
  EndModal(wxID_OK);
}

JobDef JobDlg::GetData() const {
  JobDef d;
  d.name = m_textCtrlName->GetValue();
  d.name.Trim().Trim(false);
  d.type = m_choiceJobType->GetSelection() == kJobTypePrompt
               ? JobType::kPrompt
               : JobType::kRawCommand;
  if (d.type == JobType::kPrompt) {
    d.prompt = m_textCtrlCommand->GetValue();
    const int sel = m_choiceAgent->GetSelection();
    if (sel != wxNOT_FOUND) {
      d.agentName = m_choiceAgent->GetString(sel);
    }
  } else {
    d.command = m_textCtrlCommand->GetValue();
  }
  d.intervalHours = m_spinIntervalHours->GetValue();
  d.keepTerminalOpen = m_checkBoxKeepTerminalOpen->GetValue();
  d.enabled = m_checkBoxEnabled->GetValue();
  return d;
}
void JobDlg::OnOkUI(wxUpdateUIEvent &event) {
  event.Enable(Validate().empty());
}
