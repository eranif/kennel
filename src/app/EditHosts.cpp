#include "EditHosts.hpp"
#include "core/AppManager.h"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "core/SshConfigParser.h"
#include <wx/textdlg.h>

namespace {
struct HostClientData {
  bool can_delete{true};
};
}; // namespace

EditHosts::EditHosts(wxWindow *parent, EditHostsMode mode)
    : EditHostsBase(parent), m_mode{mode} {
  InitHosts();
  if (m_dvListCtrlHosts->GetItemCount()) {
    m_dvListCtrlHosts->SelectRow(0);
  }
  m_dvListCtrlHosts->CallAfter(&wxDataViewListCtrl::SetFocus);

  if (wxButton *ok = dynamic_cast<wxButton *>(FindWindow(wxID_OK))) {
    ok->Bind(wxEVT_BUTTON, &EditHosts::OnOK, this);
  }
  PositionDialog(this, Orientation::kTop);
}

EditHosts::~EditHosts() { DeleteAll(); }

void EditHosts::OnOK(wxCommandEvent &event) {
  SaveUserHosts();
  event.Skip();
}

void EditHosts::OnDelete(wxCommandEvent &event) {
  wxUnusedVar(event);
  wxDataViewItem item = m_dvListCtrlHosts->GetSelection();
  if (!item.IsOk()) {
    return;
  }
  auto cd = GetHostData(item);
  if (!cd || !cd->can_delete) {
    return;
  }
  wxDELETE(cd);
  m_dvListCtrlHosts->DeleteItem(m_dvListCtrlHosts->ItemToRow(item));
}

void EditHosts::OnNew(wxCommandEvent &event) {
  wxUnusedVar(event);
  wxString name = wxGetTextFromUser(_("New Host Name:"), "Kennel");
  if (name.empty()) {
    return;
  }

  wxString address = wxGetTextFromUser(_("Host Address:"), "Kennel");
  if (address.empty()) {
    return;
  }
  AddHost(name, address, true);
}

void EditHosts::OnDeleteUI(wxUpdateUIEvent &event) {
  auto cd = GetHostData(m_dvListCtrlHosts->GetSelection());
  event.Enable(cd && cd->can_delete);
}

void EditHosts::InitHosts() {
  DeleteAll();

  // Load persisted user hosts first.
  if (auto result = AppManager::Get().Hosts().Load(); result.ok()) {
    for (const auto &e : result.value()) {
      AddHost(e.name, e.address, true);
    }
  }

  // Append read-only SSH config entries.
  if (auto result = SshConfigParser::Default().Parse(); result.ok()) {
    for (const auto &e : result.value()) {
      AddHost(e.name, e.address, false);
    }
  }
}

void EditHosts::SaveUserHosts() {
  std::vector<SshHostEntry> hosts;
  for (unsigned int i = 0; i < m_dvListCtrlHosts->GetItemCount(); i++) {
    auto item = m_dvListCtrlHosts->RowToItem(i);
    auto cd = GetHostData(item);
    if (!cd || !cd->can_delete) {
      continue;
    }
    SshHostEntry e;
    e.name = m_dvListCtrlHosts->GetTextValue(i, 0);
    e.address = m_dvListCtrlHosts->GetTextValue(i, 1);
    hosts.push_back(std::move(e));
  }

  if (auto st = AppManager::Get().Hosts().Save(hosts); !st.ok()) {
    KLOG_WARN() << "Could not save hosts: " << st.message();
  }
}

void EditHosts::DeleteAll() {
  for (size_t i = 0; i < m_dvListCtrlHosts->GetItemCount(); i++) {
    auto cd = GetHostData(m_dvListCtrlHosts->RowToItem(i));
    wxDELETE(cd);
  }
  m_dvListCtrlHosts->DeleteAllItems();
}

EditHosts::HostClientData *
EditHosts::GetHostData(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return nullptr;
  }
  return reinterpret_cast<HostClientData *>(
      m_dvListCtrlHosts->GetItemData(item));
}

void EditHosts::AddHost(const wxString &name, const wxString &address,
                        bool can_delete) {
  wxVector<wxVariant> cols;
  cols.push_back(name);
  cols.push_back(address);
  m_dvListCtrlHosts->AppendItem(cols,
                                reinterpret_cast<wxUIntPtr>(new HostClientData{
                                    .can_delete = can_delete,
                                }));
}

void EditHosts::OnHostActivated(wxDataViewEvent &event) {
  if (m_mode == EditHostsMode::kEdit) {
    event.Skip();
    return;
  }

  if (!UpdateSelection(event.GetItem())) {
    EndModal(wxID_CANCEL);
    return;
  }
  SaveUserHosts();
  EndModal(wxID_OK);
}

bool EditHosts::UpdateSelection(const wxDataViewItem &item) {
  auto sel = GetHostData(item);
  if (sel == nullptr) {
    EndModal(wxID_CANCEL);
    return false;
  }

  auto row = m_dvListCtrlHosts->ItemToRow(item);
  m_selectedEntry.name = m_dvListCtrlHosts->GetTextValue(row, 0);
  m_selectedEntry.address = m_dvListCtrlHosts->GetTextValue(row, 1);
  return true;
}

void EditHosts::OnHostSelected(wxDataViewEvent &event) {
  UpdateSelection(event.GetItem());
}
