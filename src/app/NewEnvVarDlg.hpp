#pragma once

#include "UI.hpp"

class NewEnvVarDlg : public NewEnvVarDlgBase {
public:
  NewEnvVarDlg(wxWindow *parent);
  ~NewEnvVarDlg() override;

  wxString GetVarName() const { return m_textCtrlName->GetValue(); }
  wxString GetVarValue() const { return m_textCtrlValue->GetValue(); }

protected:
  void OnOkUI(wxUpdateUIEvent &event) override;
};
