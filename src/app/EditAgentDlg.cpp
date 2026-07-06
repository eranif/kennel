#include "app/EditAgentDlg.hpp"
#include "FileBrowserDlg.hpp"
#include "core/KennelRemote.h"
#include "NewEnvVarDlg.hpp"
#include "app/AssetBootstrap.h"
#include "app/EditHosts.hpp"
#include "core/AppManager.h"
#include "core/Helpers.h"
#include <wx/choicdlg.h>
#include <wx/filedlg.h>
#include <wx/textdlg.h>
#include <wx/tokenzr.h>

EditAgentDlg::EditAgentDlg(wxWindow *parent, const AgentDef *agent)
    : EditAgentDlgBase(parent) {
  const auto &pref = AppManager::Get().GetPrefs();
  wxString loginShell = pref.terminalLoginShell;

  wxArrayString shells = ::FindShellNames();
  m_choiceShell->Append(shells);
  auto shellName = ::FindShellNameByCommand(pref.terminalLoginShell);
  if (shellName) {
    m_choiceShell->SetStringSelection(*shellName);
  }

  wxBusyCursor bc{};
  // Locate common binaries — locally or on the remote host.
  wxArrayString execs;
  if (agent && agent->IsRemote()) {
    auto remote =
        std::make_shared<KennelRemote>(agent->remoteHost, agent->remoteUser);
    auto clis = remote->FindCli();
    for (const auto &cli : clis) {
      execs.push_back(cli.path);
    }
  } else {
    const std::vector<std::optional<wxString>> executables{
        platform::Which("claude"),
        platform::Which("kiro-cli"),
        platform::Which("codex"),
    };
    for (const auto &e : executables) {
      if (e.has_value()) {
        execs.push_back(*e);
      }
    }
  }

  m_comboBoxExecutable->Append(execs);
  if (agent) {
    m_textCtrlName->SetValue(agent->name);
    m_comboBoxExecutable->SetValue(agent->executable);
    m_textCtrlLaunchArgs->SetValue(JoinStrings(agent->baseArgs, " "));
    m_textCtrlResumeArgs->SetValue(agent->resumeArg);
    m_textCtrlHost->SetValue(agent->remoteHost);
    m_textCtrlUser->SetValue(agent->remoteUser);
    m_textCtrlBitmap->SetValue(ResolveIconPath(agent->iconPath));
    auto shellName = ::FindShellNameByCommand(agent->loginShell);
    if (shellName) {
      m_choiceShell->SetStringSelection(*shellName);
    }
    for (const auto &[env_name, env_value] : agent->env) {
      wxVector<wxVariant> cols;
      cols.push_back(env_name);
      cols.push_back(env_value);
      m_dvListCtrlEnv->AppendItem(cols);
    }
  }

  GetSizer()->Fit(this);
  ::PositionDialog(this, Orientation::kTop);
}

EditAgentDlg::~EditAgentDlg() {}

void EditAgentDlg::OnDeleteEnv(wxCommandEvent &event) {
  auto item = m_dvListCtrlEnv->GetSelection();
  CHECK_ITEM_RETURN(item);
  m_dvListCtrlEnv->DeleteItem(m_dvListCtrlEnv->ItemToRow(item));
}

void EditAgentDlg::OnDeleteEnvUI(wxUpdateUIEvent &event) {
  event.Enable(m_dvListCtrlEnv->GetSelection().IsOk());
}

void EditAgentDlg::OnNewEnv(wxCommandEvent &event) {
  NewEnvVarDlg dlg{this};
  if (dlg.ShowModal() == wxID_OK) {
    wxVector<wxVariant> cols;
    cols.push_back(dlg.GetVarName());
    cols.push_back(dlg.GetVarValue());
    m_dvListCtrlEnv->AppendItem(cols);
  }
}

void EditAgentDlg::OnEnvActivated(wxDataViewEvent &event) {
  auto item = event.GetItem();
  CHECK_ITEM_RETURN(item);
  auto row = m_dvListCtrlEnv->ItemToRow(item);
  NewEnvVarDlg dlg{this};
  wxString name = m_dvListCtrlEnv->GetTextValue(row, 0);
  wxString value = m_dvListCtrlEnv->GetTextValue(row, 1);
  dlg.GetTextCtrlName()->SetValue(name);
  dlg.GetTextCtrlName()->Enable(false);
  dlg.GetTextCtrlValue()->SetValue(value);
  if (dlg.ShowModal() == wxID_OK) {
    CHECK_NOT_EMPTY_OR_RETURN(dlg.GetVarValue());
    m_dvListCtrlEnv->SetValue(dlg.GetVarValue(), row, 1);
  }
}

void EditAgentDlg::OnOk(wxCommandEvent &event) { event.Skip(); }

void EditAgentDlg::OnOkUI(wxUpdateUIEvent &event) {
  event.Enable(!m_textCtrlName->GetValue().empty() &&
               !m_comboBoxExecutable->GetValue().empty());
}

AgentDef EditAgentDlg::GetData() const {
  const auto &pref = AppManager::Get().GetPrefs();
  AgentDef d;
  d.name = m_textCtrlName->GetValue();
  d.executable = m_comboBoxExecutable->GetValue();
  d.resumeArg = m_textCtrlResumeArgs->GetValue();
  d.remoteHost = m_textCtrlHost->GetValue();
  d.remoteUser = m_textCtrlUser->GetValue();
  d.iconPath = m_textCtrlBitmap->GetValue();
  d.loginShell = ::FindShellCommand(m_choiceShell->GetStringSelection())
                     .value_or(pref.terminalLoginShell);
  auto baseArgs = ::wxStringTokenize(m_textCtrlLaunchArgs->GetValue(),
                                     " \t\r\n", wxTOKEN_STRTOK);
  for (auto &arg : baseArgs) {
    arg.Trim().Trim(false);
    d.baseArgs.push_back(arg);
  }

  for (auto i = 0; i < m_dvListCtrlEnv->GetItemCount(); ++i) {
    d.env.insert({m_dvListCtrlEnv->GetTextValue(i, 0),
                  m_dvListCtrlEnv->GetTextValue(i, 1)});
  }
  return d;
}

void EditAgentDlg::OnBrowseBitmap(wxCommandEvent &event) {
  wxString defaultDir = ShippedAssetsDir().GetPath();
  const wxString current = m_textCtrlBitmap->GetValue();
  if (!current.empty() && wxFileExists(current)) {
    defaultDir = wxFileName{current}.GetPath();
  }
  auto provider = std::make_unique<LocalFilesProvider>(defaultDir);
  FileBrowserDlg dlg(this, std::move(provider));

  // Only SVG files & folders are allowed
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

void EditAgentDlg::OnRemoteHost(wxCommandEvent &event) {
  EditHosts dlg{this, EditHostsMode::kInsert};
  dlg.SetLabel(_("Choose a host"));
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  m_textCtrlHost->ChangeValue(dlg.GetSelection().address);
  m_textCtrlHost->SetFocus();
}

void EditAgentDlg::OnSuggestResumeArgs(wxCommandEvent &event) {
  wxUnusedVar(event);
  wxArrayString choices{
      "kiro-cli: chat --resume",
      "claude-code: --continue",
      "codex: resume --last",
  };
  wxString choice =
      ::wxGetSingleChoice(_("Suggestions:"), "Kennel", choices, 0, this);
  if (choice.empty())
    return;

  wxString value = choice.AfterFirst(':');
  value.Trim().Trim(false);
  m_textCtrlResumeArgs->ChangeValue(value);
  m_textCtrlResumeArgs->SetFocus();
}
