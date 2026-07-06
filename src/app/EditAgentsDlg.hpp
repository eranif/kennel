#pragma once

#include "UI.hpp"
#include "core/AppManager.h"
#include "core/Config.h"

class EditAgentsDlg : public EditAgentsDlgBase {
public:
  EditAgentsDlg(wxWindow *parent);
  ~EditAgentsDlg() override;

  std::vector<AgentDef> GetAgents() const;

protected:
  void OnDelete(wxCommandEvent &event) override;
  void OnItemActivated(wxDataViewEvent &event) override;
  void OnOK(wxCommandEvent &event) override;
  void OnEditAgent(wxCommandEvent &event) override;
  void OnEditAgentUI(wxUpdateUIEvent &event) override;
  void OnNewAgent(wxCommandEvent &event) override;

  void EditSelection();
  void DeleteAll();
  void Initialise();

  AgentDef *GetAgentDef(int row = wxNOT_FOUND) const;
};
