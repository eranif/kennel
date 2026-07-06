#pragma once

#include "UI.hpp"
#include "core/Config.h"

class EditAgentDlg : public EditAgentDlgBase {
public:
  EditAgentDlg(wxWindow *parent, const AgentDef *agent);
  ~EditAgentDlg() override;

  AgentDef GetData() const;

protected:
  void OnSuggestResumeArgs(wxCommandEvent &event) override;
  void OnRemoteHost(wxCommandEvent &event) override;
  void OnBrowseBitmap(wxCommandEvent &event) override;
  void OnOk(wxCommandEvent &event) override;
  void OnOkUI(wxUpdateUIEvent &event) override;
  void OnEnvActivated(wxDataViewEvent &event) override;
  void OnDeleteEnv(wxCommandEvent &event) override;
  void OnDeleteEnvUI(wxUpdateUIEvent &event) override;
  void OnNewEnv(wxCommandEvent &event) override;

  bool m_manualEdit{false};
};
