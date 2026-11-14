#include "app/EditAgentDlg.hpp"
#include "FileBrowserDlg.hpp"
#include "NewEnvVarDlg.hpp"
#include "app/AssetBootstrap.h"
#include "app/EditHosts.hpp"
#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/Helpers.h"
#include "core/KennelRemote.h"
#include <wx/choicdlg.h>
#include <wx/filedlg.h>
#include <wx/textdlg.h>
#include <wx/tokenzr.h>

#include <thread>

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

  if (agent) {
    m_textCtrlName->SetValue(agent->name);
    m_comboBoxExecutable->SetValue(agent->executable);
    m_textCtrlLaunchArgs->SetValue(JoinStrings(agent->baseArgs, " "));
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

  // A small button with the icon of Reload, or its name when the icon is not
  // there.
  const wxBitmapBundle &reload =
      AppManager::Get().GetBitmaps().GetByAlias("reload", false);
  if (reload.IsOk()) {
    m_buttonRefreshExecutables->SetBitmap(reload);
  } else {
    m_buttonRefreshExecutables->SetLabel(_("Refresh"));
  }

  // The programs of this machine, or of the host of the agent.
  RefreshExecutables();

  GetSizer()->Fit(this);
  ::PositionDialog(this, Orientation::kTop);
}

EditAgentDlg::~EditAgentDlg() { m_alive->store(false); }

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
               ::IsSupportedClient(m_comboBoxExecutable->GetValue()));
}

AgentDef EditAgentDlg::GetData() const {
  const auto &pref = AppManager::Get().GetPrefs();
  AgentDef d;
  d.name = m_textCtrlName->GetValue();
  d.executable = m_comboBoxExecutable->GetValue();
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

  ::ApplyClientDefaults(d);

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
  // The agent is a remote one now: its programs are those of that host.
  RefreshExecutables();
}

void EditAgentDlg::RefreshExecutables() {
  const int request = ++m_executablesRequest;
  wxString host = m_textCtrlHost->GetValue();
  wxString user = m_textCtrlUser->GetValue();
  host.Trim().Trim(false);
  user.Trim().Trim(false);

  if (host.empty()) {
    SetCursor(wxNullCursor);
    // This machine: a quick look at the PATH.
    std::vector<wxString> executables;
    for (const char *name : {"claude", "kiro-cli", "codex", "opencode"}) {
      if (const auto found = platform::Which(name); found.has_value()) {
        executables.push_back(*found);
      }
    }
    ShowExecutables(executables);
    return;
  }

  // A remote host: connected and asked on a thread of its own. The thread
  // uses no member; the answer comes back to the UI thread.
  SetCursor(wxCursor(wxCURSOR_ARROWWAIT)); // Busy, but the dialog answers
  ShowExecutables({});                     // Not the programs of this machine
  std::thread([this, alive = m_alive, host, user, request] {
    KennelRemote remote(host, user);
    std::vector<wxString> executables;
    for (const auto &cli : remote.FindCli()) {
      executables.push_back(cli.path);
    }
    CallAfterIfAlive(alive, [this, request, executables] {
      if (request != m_executablesRequest) {
        return; // The user chose another host meanwhile
      }
      SetCursor(wxNullCursor);
      ShowExecutables(executables);
    });
  }).detach();
}

void EditAgentDlg::ShowExecutables(const std::vector<wxString> &executables) {
  const wxString current = m_comboBoxExecutable->GetValue();
  // A program that was in the list is of the machine of that list: it is not
  // kept for another one. What the user wrote stays.
  bool wasListed = false;
  for (unsigned i = 0; i < m_comboBoxExecutable->GetCount(); ++i) {
    wasListed = wasListed || m_comboBoxExecutable->GetString(i) == current;
  }
  m_comboBoxExecutable->Clear();
  for (const wxString &executable : executables) {
    m_comboBoxExecutable->Append(executable);
  }
  m_comboBoxExecutable->SetValue(wasListed ? wxString() : current);
}

void EditAgentDlg::OnRefreshExecutables(wxCommandEvent &event) {
  wxUnusedVar(event);
  // The programs of a host are kept for the run of the application: look again.
  KennelRemote::ClearCliCache();
  RefreshExecutables();
}
