#pragma once

#include "UI.hpp"
#include "app/AsyncGuard.hpp"
#include "core/Config.h"

class EditAgentDlg : public EditAgentDlgBase {
public:
  EditAgentDlg(wxWindow *parent, const AgentDef *agent);
  ~EditAgentDlg() override;

  AgentDef GetData() const;

protected:
  void OnRemoteHost(wxCommandEvent &event) override;
  void OnRefreshExecutables(wxCommandEvent &event) override;
  void OnBrowseBitmap(wxCommandEvent &event) override;
  void OnOk(wxCommandEvent &event) override;
  void OnOkUI(wxUpdateUIEvent &event) override;
  void OnEnvActivated(wxDataViewEvent &event) override;
  void OnDeleteEnv(wxCommandEvent &event) override;
  void OnDeleteEnvUI(wxUpdateUIEvent &event) override;
  void OnNewEnv(wxCommandEvent &event) override;

  bool m_manualEdit{false};

private:
  // Fills the list of programs for the host in the dialog: those found on this
  // machine when there is none, else those found on the host, asked on a thread
  // of its own (ssh blocks for seconds), so the dialog stays alive meanwhile.
  void RefreshExecutables();
  // Puts `executables` in the list. What the user has in the box stays, unless
  // it was one of the old list (a program of the other machine).
  void ShowExecutables(const std::vector<wxString> &executables);

  // For the thread of RefreshExecutables(), see AsyncGuard.hpp.
  AliveFlag m_alive{MakeAliveFlag()};
  // The number of the last request: the answer of an older one is dropped (the
  // user chose another host meanwhile).
  int m_executablesRequest{0};
};
