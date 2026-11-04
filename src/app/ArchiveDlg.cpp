#include "ArchiveDlg.hpp"

#include <wx/msgdlg.h>
#include <wx/wupdlock.h>

wxDEFINE_EVENT(wxEVT_ARCHIVE_RESTORE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_ARCHIVE_DELETE, wxCommandEvent);

namespace {
enum Column { kName, kAgent, kFolder, kArchivedAt };
} // namespace

ArchiveDlg::ArchiveDlg(wxWindow *parent) : ArchiveDlgBase(parent) {
  SetTitle(_("Archived Sessions"));
  SetEscapeId(wxID_CLOSE);

  constexpr int kFlags = wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE;
  m_dvListCtrl->AppendTextColumn(_("Name"), wxDATAVIEW_CELL_INERT, -1,
                                 wxALIGN_LEFT, kFlags);
  m_dvListCtrl->AppendTextColumn(_("Agent"), wxDATAVIEW_CELL_INERT, -1,
                                 wxALIGN_LEFT, kFlags);
  m_dvListCtrl->AppendTextColumn(_("Folder"), wxDATAVIEW_CELL_INERT, -1,
                                 wxALIGN_LEFT, kFlags);
  m_dvListCtrl->AppendTextColumn(_("Archived at"), wxDATAVIEW_CELL_INERT, -1,
                                 wxALIGN_LEFT, kFlags);

  m_searchCtrl->SetDescriptiveText(_("Filter"));
  m_searchCtrl->Bind(wxEVT_TEXT, &ArchiveDlg::OnFilter, this);
  m_searchCtrl->Bind(wxEVT_CHAR_HOOK, &ArchiveDlg::OnCharHook, this);
  m_buttonRestore->Bind(wxEVT_BUTTON, &ArchiveDlg::OnRestore, this);
  m_buttonDelete->Bind(wxEVT_BUTTON, &ArchiveDlg::OnDelete, this);
  m_buttonClose->Bind(wxEVT_BUTTON, &ArchiveDlg::OnClose, this);
  m_buttonRestore->Bind(wxEVT_UPDATE_UI, &ArchiveDlg::OnButtonUI, this);
  m_buttonDelete->Bind(wxEVT_UPDATE_UI, &ArchiveDlg::OnButtonUI, this);
  m_dvListCtrl->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ArchiveDlg::OnActivated,
                     this);
  m_buttonRestore->SetDefault();
  m_searchCtrl->SetFocus();
}

ArchiveDlg::~ArchiveDlg() = default;

void ArchiveDlg::SetItems(const std::vector<ArchivedSession> &items) {
  m_items = items;
  Populate();
}

void ArchiveDlg::Populate() {
  wxWindowUpdateLocker locker{m_dvListCtrl};
  m_dvListCtrl->DeleteAllItems();

  const wxString filter = m_searchCtrl->GetValue().Lower();
  // The newest first.
  for (size_t i = m_items.size(); i-- > 0;) {
    const ArchivedSession &item = m_items[i];
    const Session &s = item.session;
    if (!filter.empty()) {
      const wxString text = (s.name + "\n" + s.agentName + "\n" + s.workingDir +
                             "\n" + item.archivedAt)
                                .Lower();
      if (!text.Contains(filter)) {
        continue;
      }
    }
    wxVector<wxVariant> row;
    row.push_back(s.name);
    row.push_back(s.agentName);
    row.push_back(s.workingDir);
    row.push_back(item.archivedAt);
    m_dvListCtrl->AppendItem(row, static_cast<wxUIntPtr>(i));
  }

  // Fit the columns to what they hold.
  for (unsigned i = 0; i < m_dvListCtrl->GetColumnCount(); ++i) {
    m_dvListCtrl->GetColumn(i)->SetWidth(wxCOL_WIDTH_AUTOSIZE);
  }

  if (m_dvListCtrl->GetItemCount())
    m_dvListCtrl->SelectRow(0);
}

int ArchiveDlg::SelectedIndex() const {
  const wxDataViewItem item = m_dvListCtrl->GetSelection();
  if (!item.IsOk()) {
    return -1;
  }
  return static_cast<int>(m_dvListCtrl->GetItemData(item));
}

void ArchiveDlg::SendRequest(const wxEventTypeTag<wxCommandEvent> &type) {
  const int index = SelectedIndex();
  if (index < 0) {
    return;
  }
  wxCommandEvent event{type};
  event.SetEventObject(this);
  event.SetInt(index);
  GetEventHandler()->ProcessEvent(event);
}

void ArchiveDlg::OnFilter(wxCommandEvent &) { Populate(); }

void ArchiveDlg::OnCharHook(wxKeyEvent &event) {
  switch (event.GetKeyCode()) {
  case WXK_DOWN: {
    CallAfter([this]() {
      int selection = m_dvListCtrl->GetSelectedRow();
      if (selection + 1 < m_dvListCtrl->GetItemCount()) {
        m_dvListCtrl->SelectRow(selection + 1);
      }
    });
  } break;
  case WXK_UP: {
    CallAfter([this]() {
      int selection = m_dvListCtrl->GetSelectedRow();
      if (selection > 0) {
        m_dvListCtrl->SelectRow(selection - 1);
      }
    });
  } break;
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    if (m_dvListCtrl->GetSelectedRow() != wxNOT_FOUND) {
      CallAfter([this] {
        wxCommandEvent dummy;
        OnRestore(dummy);
      });
    }
    break;

  default:
    event.Skip();
    break;
  }
}

void ArchiveDlg::OnRestore(wxCommandEvent &) {
  SendRequest(wxEVT_ARCHIVE_RESTORE);
}

void ArchiveDlg::OnDelete(wxCommandEvent &) {
  const int index = SelectedIndex();
  if (index < 0 || static_cast<size_t>(index) >= m_items.size()) {
    return;
  }
  const int answer =
      wxMessageBox(wxString::Format(_("Delete '%s' from the archive?"),
                                    m_items[index].session.name),
                   "Kennel", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
  if (answer == wxYES) {
    SendRequest(wxEVT_ARCHIVE_DELETE);
  }
}

void ArchiveDlg::OnClose(wxCommandEvent &) { EndModal(wxID_CLOSE); }

void ArchiveDlg::OnActivated(wxDataViewEvent &) {
  SendRequest(wxEVT_ARCHIVE_RESTORE);
}

void ArchiveDlg::OnButtonUI(wxUpdateUIEvent &event) {
  event.Enable(SelectedIndex() >= 0);
}
