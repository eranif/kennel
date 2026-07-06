#include "NewAgentWizard.hpp"
#include "FileBrowserDlg.hpp"
#include "NewEnvVarDlg.hpp"
#include "app/AssetBootstrap.h"
#include "app/EditHosts.hpp"
#include "core/AppManager.h"
#include "core/Helpers.h"
#include "core/KennelRemote.h"

#include <wx/choicdlg.h>

#include <wx/bmpbndl.h>

namespace {
wxBitmapBundle LoadWizardBitmap(const wxString &name) {
  const wxString path = ResolveIconPath(name);
  if (path.empty() || !wxFileExists(path)) {
    return wxBitmapBundle{};
  }
  return wxBitmapBundle::FromSVGFile(path, wxSize(120, 400));
}
} // namespace

NewAgentWizard::NewAgentWizard(wxWindow *parent) : NewAgentWizardBase(parent) {
  SetLabel(_("Configure New Agent"));
  m_bannerLocalOrRemoteBitmap->SetBitmap(
      LoadWizardBitmap("wizard-connection.svg"));
  m_bannerWhatToLaunchBitmap->SetBitmap(LoadWizardBitmap("wizard-details.svg"));
  m_bannerAdvancedBimap->SetBitmap(LoadWizardBitmap("wizard-advanced.svg"));
  PopulateLoginShells();
  Bind(wxEVT_UPDATE_UI, &NewAgentWizard::OnNextUI, this, wxID_FORWARD);
  Bind(wxEVT_WIZARD_PAGE_SHOWN, &NewAgentWizard::OnPageShown, this);
  PositionDialog(this);
}

void NewAgentWizard::OnPageShown(wxWizardEvent &event) {
  event.Skip();
  if (!m_centered) {
    m_centered = true;
    CenterOnParent();
  }
  auto *page = GetCurrentPage();
  if (page == m_wizardPageLocalOrRemote) {
    m_checkBoxLocalHost->SetFocus();
  } else if (page == m_wizardPageWhatToLaunch) {
    PopulateExecutables();
    m_textCtrlName->SetFocus();
  } else if (page == m_wizardPageAdvanced) {
    m_choiceShell->SetFocus();
  }
}

void NewAgentWizard::PopulateExecutables() {
  m_comboBoxExecutable->Clear();
  wxBusyCursor bc{};

  wxArrayString execs;
  if (m_checkBoxLocalHost->IsChecked()) {
    const std::vector<std::optional<wxString>> found{
        platform::Which("claude"),
        platform::Which("kiro-cli"),
        platform::Which("codex"),
    };
    for (const auto &e : found) {
      if (e.has_value()) {
        execs.push_back(*e);
      }
    }
  } else {
    const wxString host = m_textCtrlHost->GetValue();
    const wxString user = m_textCtrlUser->GetValue();
    if (!host.empty()) {
      auto remote = std::make_shared<KennelRemote>(host, user);
      auto clis = remote->FindCli();
      for (const auto &cli : clis) {
        execs.push_back(cli.path);
      }
    }
  }

  m_comboBoxExecutable->Append(execs);
  if (!execs.empty()) {
    m_comboBoxExecutable->SetSelection(0);
  }
}

void NewAgentWizard::OnNextUI(wxUpdateUIEvent &event) {
  if (GetCurrentPage() == m_wizardPageWhatToLaunch) {
    wxString name = m_textCtrlName->GetValue();
    wxString exec = m_comboBoxExecutable->GetValue();
    wxString bitmap = m_textCtrlBitmap->GetValue();
    name.Trim().Trim(false);
    exec.Trim().Trim(false);
    bitmap.Trim().Trim(false);
    event.Enable(!name.empty() && !exec.empty() && !bitmap.empty() &&
                 wxFileExists(bitmap));
  } else {
    event.Enable(true);
  }
}

NewAgentWizard::~NewAgentWizard() {}

AgentDef NewAgentWizard::GetData() const {
  AgentDef d;

  // Page 1: connection
  if (!m_checkBoxLocalHost->IsChecked()) {
    d.remoteHost = m_textCtrlHost->GetValue();
    d.remoteUser = m_textCtrlUser->GetValue();
  }

  // Page 2: details
  d.name = m_textCtrlName->GetValue();
  d.executable = m_comboBoxExecutable->GetValue();
  d.resumeArg = m_textCtrlResumeArgs->GetValue();
  d.iconPath = m_textCtrlBitmap->GetValue();

  auto baseArgs = ::wxStringTokenize(m_textCtrlLaunchArgs->GetValue(),
                                     " \t\r\n", wxTOKEN_STRTOK);
  for (auto &arg : baseArgs) {
    arg.Trim().Trim(false);
    d.baseArgs.push_back(arg);
  }

  // Page 3: advanced
  d.loginShell = FindShellCommand(m_choiceShell->GetStringSelection())
                     .value_or(wxString{});

  for (int i = 0; i < m_dvListCtrlEnv->GetItemCount(); ++i) {
    d.env.insert({m_dvListCtrlEnv->GetTextValue(i, 0),
                  m_dvListCtrlEnv->GetTextValue(i, 1)});
  }

  return d;
}

void NewAgentWizard::OnEnableRemoteUI(wxUpdateUIEvent &event) {
  event.Enable(!m_checkBoxLocalHost->IsChecked());
}

void NewAgentWizard::OnBrowseHosts(wxCommandEvent &event) {
  EditHosts dlg{this, EditHostsMode::kInsert};
  dlg.SetLabel(_("Choose a host"));
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  m_textCtrlHost->ChangeValue(dlg.GetSelection().address);
  m_textCtrlHost->SetFocus();
}

void NewAgentWizard::OnBrowseBitmap(wxCommandEvent &event) {
  wxString defaultDir = ShippedAssetsDir().GetPath();
  const wxString current = m_textCtrlBitmap->GetValue();
  if (!current.empty() && wxFileExists(current)) {
    defaultDir = wxFileName{current}.GetPath();
  }
  auto provider = std::make_unique<LocalFilesProvider>(defaultDir);
  FileBrowserDlg dlg(this, std::move(provider));
  dlg.SetFilterFunction([](const FileProvider::File &file) -> bool {
    if (file.IsFile() && !file.path.Lower().EndsWith(".svg")) {
      return false;
    }
    return true;
  });
  if (dlg.ShowModal() == wxID_OK) {
    m_textCtrlBitmap->SetValue(dlg.GetPath());
  }
}

void NewAgentWizard::OnBrowseResumeArgs(wxCommandEvent &event) {
  wxUnusedVar(event);
  wxArrayString choices{
      "kiro-cli: chat --resume",
      "claude-code: --continue",
      "codex: resume --last",
  };
  wxString choice =
      ::wxGetSingleChoice(_("Suggestions:"), "Kennel", choices, 0, this);
  if (choice.empty()) {
    return;
  }
  wxString value = choice.AfterFirst(':');
  value.Trim().Trim(false);
  m_textCtrlResumeArgs->ChangeValue(value);
  m_textCtrlResumeArgs->SetFocus();
}

void NewAgentWizard::OnDeleteEnv(wxCommandEvent &event) {
  auto item = m_dvListCtrlEnv->GetSelection();
  CHECK_ITEM_RETURN(item);
  m_dvListCtrlEnv->DeleteItem(m_dvListCtrlEnv->ItemToRow(item));
}

void NewAgentWizard::OnDeleteEnvUI(wxUpdateUIEvent &event) {
  event.Enable(m_dvListCtrlEnv->GetSelection().IsOk());
}

void NewAgentWizard::OnNewEnv(wxCommandEvent &event) {
  NewEnvVarDlg dlg{this};
  if (dlg.ShowModal() == wxID_OK) {
    wxVector<wxVariant> cols;
    cols.push_back(dlg.GetVarName());
    cols.push_back(dlg.GetVarValue());
    m_dvListCtrlEnv->AppendItem(cols);
  }
}

void NewAgentWizard::PopulateLoginShells() {
  const auto &pref = AppManager::Get().GetPrefs();
  wxString loginShell = pref.terminalLoginShell;

  wxArrayString shells = ::FindShellNames();
  m_choiceShell->Append(shells);
  auto shellName = ::FindShellNameByCommand(pref.terminalLoginShell);
  if (shellName) {
    m_choiceShell->SetStringSelection(*shellName);
  }
}