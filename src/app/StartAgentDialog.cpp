#include "app/StartAgentDialog.hpp"

#include "app/FileBrowserDlg.hpp"
#include "app/MainFrame.h"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/UiPrefs.h"

#include "core/Helpers.h"
#include <wx/clntdata.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

namespace {
wxString NormaliseFilename(const wxString &str) {
  wxString fixed;
  fixed.reserve(str.size());
  for (const auto &ch : str) {
    if (wxIsalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
      fixed << ch;
      continue;
    }
    fixed << '_';
  }
  return fixed;
}
} // namespace

StartAgentDialog::StartAgentDialog(wxWindow *parent)
    : StartAgentDialogBase(parent) {
  wxString existMessage;
  existMessage << wxT("⚠") << _(" Session name already exists");
  m_staticTextErrorMessage->SetLabel(existMessage);
  PopulateClients();
  GetSizer()->Fit(this);
  Layout();
  CenterOnParent();
}

StartAgentDialog::~StartAgentDialog() {}

void StartAgentDialog::PopulateClients() {
  m_choiceClients->Clear();
  const AdapterRegistry &registry = AppManager::Get().Adapters();

  for (const wxString &name : registry.AgentNames()) {
    const int row = m_choiceClients->Append(name);
    m_choiceClients->SetClientObject(row, new wxStringClientData(name));
  }

  if (!m_choiceClients->IsEmpty()) {
    m_choiceClients->SetSelection(0);
  }

  // Existing logical groups. Exclude "Terminals"
  m_comboBoxGroup->Clear();
  m_comboBoxGroup->Append(
      AppManager::Get().Groups([](const Session &sess) -> bool {
        if (sess.plainTerminal)
          return false;
        return true;
      }));
  if (m_comboBoxGroup->GetCount() > 0) {
    m_comboBoxGroup->SetSelection(0);
  }

  const auto &prefs = AppManager::Get().GetPrefs();
  m_comboBoxWorkingDir->Clear();
  if (!prefs.recentWorkingDirs.empty()) {
    m_comboBoxWorkingDir->Append(wxEmptyString);
    for (const wxString &d : prefs.recentWorkingDirs) {
      m_comboBoxWorkingDir->Append(d);
    }
  } else {
    m_comboBoxWorkingDir->ChangeValue(wxEmptyString);
  }

  CenterOnParent();
  Move(GetPosition().x, GetParent()->GetPosition().y);
}

wxString StartAgentDialog::SelectedAgentId() const {
  const int sel = m_choiceClients->GetSelection();
  if (sel == wxNOT_FOUND) {
    return wxString{};
  }
  auto *data =
      static_cast<wxStringClientData *>(m_choiceClients->GetClientObject(sel));
  return data ? data->GetData() : wxString{};
}

bool StartAgentDialog::SetSelectedClientName(const wxString &name) {
  for (unsigned int i = 0; i < m_choiceClients->GetCount(); ++i) {
    auto *data =
        static_cast<wxStringClientData *>(m_choiceClients->GetClientObject(i));
    if (data && data->GetData() == name) {
      m_choiceClients->SetSelection(static_cast<int>(i));
      return true;
    }
  }
  return false;
}

void StartAgentDialog::SetSelectedGroup(const wxString &name) {
  m_comboBoxGroup->SetValue(name);
}

void StartAgentDialog::OnOkUI(wxUpdateUIEvent &event) {
  wxString name = m_textCtrlName->GetValue();
  name.Trim().Trim(false);
  event.Enable(!name.IsEmpty() &&
               !GetMainFrame()->GetMainView()->IsNameExist(name) &&
               !m_comboBoxWorkingDir->GetValue().IsEmpty());
}

wxString StartAgentDialog::MakeGroupName() const {
  wxString groupName = m_comboBoxGroup->GetValue();
  if (groupName.empty()) {
    groupName = _("Default");
  }
  return groupName;
}

wxString StartAgentDialog::MakeWorkingDir(bool checked) const {
  wxString folder = m_comboBoxWorkingDir->GetValue();
  if (checked) {
    folder << wxFileName::GetPathSeparator()
           << NormaliseFilename(m_textCtrlName->GetValue());
  }
  return folder;
}

NewSessionRequest StartAgentDialog::GetRequest() const {
  return NewSessionRequest{
      .name = m_textCtrlName->GetValue(),
      .agentName = SelectedAgentId(),
      .workingDir = MakeWorkingDir(m_checkBoxInnderFolder->IsChecked()),
      .resume = m_checkBoxResume->IsChecked(),
      .groupName = MakeGroupName(),
  };
}

void StartAgentDialog::OnInnerFolder(wxCommandEvent &event) { event.Skip(); }

void StartAgentDialog::OnBrowseWD(wxCommandEvent &event) {
  auto agentName = SelectedAgentId();
  const AgentDef *agent = AppManager::Get().Adapters().FindAgent(agentName);
  if (agent && agent->IsRemote()) {
    auto remote =
        std::make_shared<KennelRemote>(agent->remoteHost, agent->remoteUser);
    auto provider =
        std::make_unique<RemoteFilesProvider>(remote, wxEmptyString, true);
    FileBrowserDlg dlg{this, std::move(provider)};
    if (dlg.ShowModal() == wxID_OK) {
      m_comboBoxWorkingDir->SetValue(dlg.GetPath());
    }
    return;
  } else if (agent && agent->IsWSL() &&
             ::FindShellNameByCommand(agent->loginShell).has_value()) {
    wxString wslname = *::FindShellNameByCommand(agent->loginShell);
    wslname = wslname.Mid(5);
    auto provider =
        std::make_unique<WSLFilesProvider>(wslname, wxEmptyString, true);
    FileBrowserDlg dlg{this, std::move(provider)};
    if (dlg.ShowModal() == wxID_OK) {
      m_comboBoxWorkingDir->SetValue(dlg.GetPath());
    }
    return;
  }

  // Create provider for directories only
  auto provider = std::make_unique<LocalFilesProvider>(::wxGetHomeDir(), true);
  FileBrowserDlg dlg{this, std::move(provider)};
  if (dlg.ShowModal() == wxID_OK) {
    m_comboBoxWorkingDir->SetValue(dlg.GetPath());
  }
}

void StartAgentDialog::OnBrowseWdUI(wxUpdateUIEvent &event) {
  auto agentName = SelectedAgentId();
  const AgentDef *agent = AppManager::Get().Adapters().FindAgent(agentName);
  event.Enable(agent);
}

void StartAgentDialog::OnNameUpdated(wxCommandEvent &event) {
  wxUnusedVar(event);
  wxString name = m_textCtrlName->GetValue();
  name.Trim().Trim(false);
  if (GetMainFrame()->GetMainView()->IsNameExist(name)) {
    if (!m_staticTextErrorMessage->IsShown()) {
      m_staticTextErrorMessage->Show();
      GetSizer()->Fit(this);
      GetSizer()->Layout();
    }
  } else if (m_staticTextErrorMessage->IsShown()) {
    m_staticTextErrorMessage->Hide();
    GetSizer()->Fit(this);
    GetSizer()->Layout();
  }
}
