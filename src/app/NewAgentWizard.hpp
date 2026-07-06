#pragma once

#include "UI.hpp"
#include "core/Config.h"

class NewAgentWizard : public NewAgentWizardBase {
public:
  NewAgentWizard(wxWindow *parent);
  ~NewAgentWizard() override;

  AgentDef GetData() const;

protected:
  void OnBrowseBitmap(wxCommandEvent &event) override;
  void OnBrowseResumeArgs(wxCommandEvent &event) override;
  void OnDeleteEnv(wxCommandEvent &event) override;
  void OnDeleteEnvUI(wxUpdateUIEvent &event) override;
  void OnNewEnv(wxCommandEvent &event) override;
  void OnBrowseHosts(wxCommandEvent &event) override;
  void OnEnableRemoteUI(wxUpdateUIEvent &event) override;

private:
  void OnNextUI(wxUpdateUIEvent &event);
  void OnPageShown(wxWizardEvent &event);
  void PopulateExecutables();
  void PopulateLoginShells();

  bool m_centered = false;
};
