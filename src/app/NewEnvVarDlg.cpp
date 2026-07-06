#include "NewEnvVarDlg.hpp"

NewEnvVarDlg::NewEnvVarDlg(wxWindow *parent) : NewEnvVarDlgBase(parent) {
  CenterOnParent();

  CallAfter([this]() {
    if (m_textCtrlName->IsEnabled()) {
      m_textCtrlName->SetFocus();
    } else {
      m_textCtrlValue->SetFocus();
    }
  });
}

NewEnvVarDlg::~NewEnvVarDlg() {}

void NewEnvVarDlg::OnOkUI(wxUpdateUIEvent &event) {
  event.Enable(!m_textCtrlName->IsEmpty() && !m_textCtrlValue->IsEmpty());
}
