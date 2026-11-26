#include "app/BroadcastDlg.hpp"

#include <wx/checklst.h>
#include <wx/textctrl.h>

BroadcastDlg::BroadcastDlg(wxWindow *parent, const std::vector<Target> &targets)
    : BroadcastDlgBase(parent), m_targets(targets) {
  for (const Target &target : m_targets) {
    const int row = m_checkListSessions->Append(target.label);
    m_checkListSessions->Check(row, true);
  }
  // The OK button of the standard button sizer sends.
  m_buttonSend->SetLabel(_("Send"));
  GetSizer()->Fit(this);
  CenterOnParent();
  m_textCtrlMessage->SetFocus();
}

BroadcastDlg::~BroadcastDlg() {}

std::vector<SessionRef> BroadcastDlg::GetSelectedSessions() const {
  std::vector<SessionRef> selected;
  for (unsigned int i = 0; i < m_checkListSessions->GetCount(); ++i) {
    if (m_checkListSessions->IsChecked(i) && i < m_targets.size()) {
      selected.push_back(m_targets[i].ref);
    }
  }
  return selected;
}

wxString BroadcastDlg::GetMessage() const {
  wxString text = m_textCtrlMessage->GetValue();
  text.Replace("\r\n", " ");
  text.Replace("\n", " ");
  text.Replace("\r", " ");
  text.Trim().Trim(false);
  return text;
}

void BroadcastDlg::CheckAll(bool check) {
  for (unsigned int i = 0; i < m_checkListSessions->GetCount(); ++i) {
    m_checkListSessions->Check(i, check);
  }
}

void BroadcastDlg::OnCheckAll(wxCommandEvent &event) {
  wxUnusedVar(event);
  CheckAll(true);
}

void BroadcastDlg::OnUncheckAll(wxCommandEvent &event) {
  wxUnusedVar(event);
  CheckAll(false);
}

void BroadcastDlg::OnSendUI(wxUpdateUIEvent &event) {
  wxArrayInt checked;
  event.Enable(m_checkListSessions->GetCheckedItems(checked) > 0 &&
               !GetMessage().empty());
}
