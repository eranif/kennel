#include "GotoAnythingDlg.hpp"

#include "app/PageViewEvent.hpp"
#include "app/PopupStyle.hpp"

#include "app/DataViewUtils.hpp"
#include <algorithm>
#include <wx/settings.h>
#include <wx/tokenzr.h>
#include <wx/wupdlock.h>

namespace {
// How many rows Page Up / Page Down move.
constexpr int kPageSize = 10;
} // namespace

#ifdef __WXMAC__
constexpr long style = 0;
#else
constexpr long style = wxCAPTION;
#endif

GotoAnythingDlg::GotoAnythingDlg(wxWindow *parent)
    : GotoAnythingDlgBase(parent, wxID_ANY, _("Goto Anything"),
                          wxDefaultPosition, wxDefaultSize, style) {
  dv::StylePageView(m_dvListCtrl);
  m_dvListCtrl->Bind(wxEVT_SIZE, &GotoAnythingDlg::OnListSize, this);

  m_searchCtrl->SetDescriptiveText(_("Type to search..."));
  m_searchCtrl->Bind(wxEVT_TEXT, &GotoAnythingDlg::OnText, this);
  m_dvListCtrl->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,
                     &GotoAnythingDlg::OnActivated, this);
  // The keys of the search box: the dialog has no buttons to take them.
  Bind(wxEVT_CHAR_HOOK, &GotoAnythingDlg::OnCharHook, this);
  StylePopupWindow(this, 10.0);
  CenterOnParent();
  m_searchCtrl->SetFocus();
}

GotoAnythingDlg::~GotoAnythingDlg() = default;

void GotoAnythingDlg::SetItems(std::vector<GotoItem> items) {
  m_items = std::move(items);
  Populate();
}

const GotoItem *GotoAnythingDlg::GetChosen() const {
  if (m_chosen < 0 || static_cast<size_t>(m_chosen) >= m_items.size()) {
    return nullptr;
  }
  return &m_items[m_chosen];
}

void GotoAnythingDlg::Populate() {
  wxWindowUpdateLocker locker{m_dvListCtrl};
  m_dvListCtrl->DeleteAllItems();

  wxArrayString words;
  wxStringTokenizer tokenizer(m_searchCtrl->GetValue().Lower());
  while (tokenizer.HasMoreTokens()) {
    words.Add(tokenizer.GetNextToken());
  }

  for (size_t i = 0; i < m_items.size(); ++i) {
    const GotoItem &item = m_items[i];
    const wxString label = item.label.Lower();
    bool match = true;
    for (const wxString &word : words) {
      if (!label.Contains(word)) {
        match = false;
        break;
      }
    }
    if (!match) {
      continue;
    }
    wxVector<wxVariant> row;
    row.push_back(item.label);
    row.push_back(item.shortcut);
    m_dvListCtrl->AppendItem(row, static_cast<wxUIntPtr>(i));
  }

  if (m_dvListCtrl->GetItemCount() > 0) {
    m_dvListCtrl->SelectRow(0);
  }
  dv::AutoSizeColumns(m_dvListCtrl);
  // FitColumns();
}

void GotoAnythingDlg::FitColumns() {
  // The shortcut is as wide as its longest text; the action takes the rest.
  wxDataViewColumn *action = m_dvListCtrl->GetColumn(0);
  wxDataViewColumn *shortcut = m_dvListCtrl->GetColumn(1);
  // Measured here: the width of an automatic column is not known until it is
  // drawn (and the columns start with an automatic width).
  int shortcutWidth = m_dvListCtrl->GetTextExtent(shortcut->GetTitle()).x;
  for (unsigned row = 0; row < m_dvListCtrl->GetItemCount(); ++row) {
    shortcutWidth = std::max(
        shortcutWidth,
        m_dvListCtrl->GetTextExtent(m_dvListCtrl->GetTextValue(row, 1)).x);
  }
  shortcutWidth += FromDIP(30); // Padding and the sort arrow
  shortcut->SetWidth(shortcutWidth);
  // Room for the scroll bar and the borders.
  const int rest =
      m_dvListCtrl->GetClientSize().GetWidth() - shortcutWidth - FromDIP(30);
  action->SetWidth(std::max(rest, FromDIP(100)));
}

void GotoAnythingDlg::OnListSize(wxSizeEvent &event) {
  event.Skip();
  dv::AutoSizeColumns(m_dvListCtrl);
}

void GotoAnythingDlg::Accept() {
  const wxDataViewItem item = m_dvListCtrl->GetSelection();
  if (!item.IsOk()) {
    return;
  }
  m_chosen = static_cast<int>(m_dvListCtrl->GetItemData(item));
  EndModal(wxID_OK);
}

void GotoAnythingDlg::MoveSelection(int delta) {
  const int count = static_cast<int>(m_dvListCtrl->GetItemCount());
  if (count == 0) {
    return;
  }
  int row = m_dvListCtrl->GetSelectedRow();
  row = row == wxNOT_FOUND ? 0 : row + delta;
  row = std::clamp(row, 0, count - 1);
  m_dvListCtrl->SelectRow(row);
  m_dvListCtrl->EnsureVisible(m_dvListCtrl->RowToItem(row));
}

void GotoAnythingDlg::OnText(wxCommandEvent &) { Populate(); }

void GotoAnythingDlg::OnActivated(wxDataViewEvent &) { Accept(); }

void GotoAnythingDlg::OnCharHook(wxKeyEvent &event) {
  switch (event.GetKeyCode()) {
  case WXK_ESCAPE:
    EndModal(wxID_CANCEL);
    break;
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    Accept();
    break;
  case WXK_DOWN:
    MoveSelection(1);
    break;
  case WXK_UP:
    MoveSelection(-1);
    break;
  case WXK_PAGEDOWN:
    MoveSelection(kPageSize);
    break;
  case WXK_PAGEUP:
    MoveSelection(-kPageSize);
    break;
  default:
    event.Skip();
    break;
  }
}

int GotoAnythingDlg::ShowModal() {
#ifdef __WXMAC__
  const int captionHeight = FromDIP(32);
#else
  const int captionHeight = wxSystemSettings::GetMetric(wxSYS_CAPTION_Y);
#endif

  Move(GetPosition().x, GetParent()->GetPosition().y + captionHeight);
  return wxDialog::ShowModal();
}
