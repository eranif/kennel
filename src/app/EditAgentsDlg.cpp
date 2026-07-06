#include "app/EditAgentsDlg.hpp"
#include "app/AssetBootstrap.h"
#include "app/EditAgentDlg.hpp"
#include "core/Bitmaps.h"
#include "core/Helpers.h"

EditAgentsDlg::EditAgentsDlg(wxWindow *parent) : EditAgentsDlgBase(parent) {
  Initialise();
  m_dvListCtrlAgents->CallAfter(&wxDataViewListCtrl::SetFocus);
  GetSizer()->Fit(this);
  PositionDialog(this, Orientation::kTop);
}

EditAgentsDlg::~EditAgentsDlg() { DeleteAll(); }

void EditAgentsDlg::OnEditAgent(wxCommandEvent &event) {
  wxUnusedVar(event);
  EditSelection();
}

void EditAgentsDlg::EditSelection() {
  AgentDef *selection = GetAgentDef();
  if (selection == nullptr) {
    return;
  }

  EditAgentDlg editDlg{this, selection};
  editDlg.SetLabel(_("Edit Agent"));

  if (editDlg.ShowModal() == wxID_OK) {
    wxDELETE(selection);
    selection = new AgentDef{editDlg.GetData()};
    auto icon_path = ResolveIconPath(selection->iconPath);
    AppManager::Get().GetBitmaps().Load(icon_path);
    AppManager::Get().GetBitmaps().AddAlias(icon_path, selection->name);

    m_dvListCtrlAgents->SetItemData(m_dvListCtrlAgents->GetSelection(),
                                    reinterpret_cast<wxUIntPtr>(selection));

    auto bmp =
        AppManager::Get().GetBitmaps().GetByAlias(selection->name, false);
    wxDataViewIconText entry(selection->name, bmp);
    wxVariant v;
    v << entry;
    m_dvListCtrlAgents->SetValue(v, m_dvListCtrlAgents->GetSelectedRow(), 0);
  }
}

void EditAgentsDlg::OnEditAgentUI(wxUpdateUIEvent &event) {
  event.Enable(GetAgentDef() != nullptr);
}

void EditAgentsDlg::OnNewAgent(wxCommandEvent &event) {
  EditAgentDlg editDlg{this, nullptr};
  if (editDlg.ShowModal() == wxID_OK) {
    auto *data = new AgentDef{editDlg.GetData()};
    auto icon_path = ResolveIconPath(data->iconPath);
    AppManager::Get().GetBitmaps().Load(icon_path);
    AppManager::Get().GetBitmaps().AddAlias(icon_path, data->name);
    auto bmp = AppManager::Get().GetBitmaps().GetByAlias(data->name, false);

    wxVector<wxVariant> cols;
    wxDataViewIconText entry(data->name, bmp);
    wxVariant v;
    v << entry;
    cols.push_back(v);
    m_dvListCtrlAgents->AppendItem(cols, reinterpret_cast<wxUIntPtr>(data));
  }
}

AgentDef *EditAgentsDlg::GetAgentDef(int row) const {
  wxDataViewItem item{nullptr};
  if (row != wxNOT_FOUND) {
    item = m_dvListCtrlAgents->RowToItem(row);
  } else {
    item = m_dvListCtrlAgents->GetSelection();
  }
  if (!item.IsOk()) {
    return nullptr;
  }
  return reinterpret_cast<AgentDef *>(m_dvListCtrlAgents->GetItemData(item));
}

void EditAgentsDlg::OnOK(wxCommandEvent &event) {
  event.Skip();
  EndModal(wxID_OK);
}

void EditAgentsDlg::DeleteAll() {
  for (int row = 0; row < m_dvListCtrlAgents->GetItemCount(); ++row) {
    auto *def = GetAgentDef(row);
    wxDELETE(def);
  }
  m_dvListCtrlAgents->DeleteAllItems();
}

void EditAgentsDlg::Initialise() {
  DeleteAll();
  const auto &agents = AppManager::Get().Config().agents;
  for (const auto &agent : agents) {
    wxVector<wxVariant> cols;
    auto bmp = AppManager::Get().GetBitmaps().GetByAlias(agent.name, false);
    wxDataViewIconText entry(agent.name, bmp);
    wxVariant v;
    v << entry;
    cols.push_back(v);
    m_dvListCtrlAgents->AppendItem(
        cols, reinterpret_cast<wxUIntPtr>(new AgentDef(agent)));
  }
  if (m_dvListCtrlAgents->GetItemCount()) {
    m_dvListCtrlAgents->SelectRow(0);
  }
}

void EditAgentsDlg::OnItemActivated(wxDataViewEvent &event) {
  wxUnusedVar(event);
  EditSelection();
}

std::vector<AgentDef> EditAgentsDlg::GetAgents() const {
  std::vector<AgentDef> agents;
  for (auto i = 0; i < m_dvListCtrlAgents->GetItemCount(); ++i) {
    auto *ad = GetAgentDef(i);
    if (ad) {
      agents.push_back(*ad);
    }
  }
  return agents;
}

void EditAgentsDlg::OnDelete(wxCommandEvent &event) {
  auto *p = GetAgentDef();
  CHECK_NOT_NULL_RETURN(p);
  wxDELETE(p);
  m_dvListCtrlAgents->DeleteItem(m_dvListCtrlAgents->GetSelectedRow());
}
