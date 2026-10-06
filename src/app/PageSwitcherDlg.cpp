#include "PageSwitcherDlg.hpp"

#include <wx/dataview.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <algorithm>

namespace {
constexpr int kMaxVisibleRows = 12;
constexpr int kMinWidth = 500;
constexpr int kMinHeight = 300;
constexpr int kMaxWidth = 700;
constexpr int kPollMs = 30;
constexpr int kIconSize = 16;
#ifdef __WXOSX__
constexpr int kHotKeyNext = 1;
constexpr int kHotKeyPrev = 2;
#endif
} // namespace

PageSwitcherDlg::PageSwitcherDlg(wxWindow *parent,
                                 const std::vector<PageSwitcherItem> &items,
                                 bool forward)
    : wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
               wxDefaultSize, wxBORDER_SIMPLE | wxSTAY_ON_TOP),
      m_timer(this) {
  m_list =
      new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxDV_NO_HEADER | wxDV_SINGLE | wxBORDER_NONE);

  int width = kMinWidth;
  for (const auto &item : items) {
    width = std::max(width, GetTextExtent(item.label).GetWidth() + 80);
  }
  width = std::min(width, kMaxWidth);
  const int rowHeight = std::max(m_list->GetCharHeight(), kIconSize) + 10;
  const int rows = std::min(static_cast<int>(items.size()), kMaxVisibleRows);

  m_list->AppendIconTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT,
                               width - 24);
  for (const auto &item : items) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(wxDataViewIconText(item.label, item.icon)));
    m_list->AppendItem(row);
  }
  m_list->SetMinSize(wxSize(width, std::max(rows * rowHeight, kMinHeight)));

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand().Border(wxALL, 4));
  SetSizerAndFit(sizer);

  Bind(wxEVT_CHAR_HOOK, &PageSwitcherDlg::OnCharHook, this);
  Bind(wxEVT_TIMER, &PageSwitcherDlg::OnTimer, this);
  m_list->Bind(wxEVT_KEY_UP, &PageSwitcherDlg::OnKeyUp, this);
  m_list->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &PageSwitcherDlg::OnActivated,
               this);

  const int count = static_cast<int>(items.size());
  if (count > 0) {
    const int initial = forward ? std::min(1, count - 1) : count - 1;
    m_list->SelectRow(static_cast<unsigned int>(initial));
    m_list->EnsureVisible(m_list->RowToItem(initial));
  }
#ifdef __WXOSX__
  // macOS uses Ctrl+Tab to move the keyboard focus before the list gets the
  // key, so wxEVT_CHAR_HOOK never sees it. A hot key is caught before that.
  // It is unregistered in the destructor.
  RegisterHotKey(kHotKeyNext, wxMOD_RAW_CONTROL, WXK_TAB);
  RegisterHotKey(kHotKeyPrev, wxMOD_RAW_CONTROL | wxMOD_SHIFT, WXK_TAB);
  Bind(wxEVT_HOTKEY, [this](wxKeyEvent &) { Advance(true); }, kHotKeyNext);
  Bind(wxEVT_HOTKEY, [this](wxKeyEvent &) { Advance(false); }, kHotKeyPrev);
#endif
  m_list->SetFocus();
  CentreOnParent();
  // Releasing Ctrl normally arrives as a key-up on the list, but poll the
  // modifier state too in case the release happened before we had focus.
  m_timer.Start(kPollMs);
}

PageSwitcherDlg::~PageSwitcherDlg() {
#ifdef __WXOSX__
  UnregisterHotKey(kHotKeyNext);
  UnregisterHotKey(kHotKeyPrev);
#endif
}

int PageSwitcherDlg::GetSelectedIndex() const {
  const int row = m_list->GetSelectedRow();
  return row == wxNOT_FOUND ? wxNOT_FOUND : row;
}

void PageSwitcherDlg::Advance(bool forward) {
  const int count = static_cast<int>(m_list->GetItemCount());
  if (count == 0) {
    return;
  }
  const int current = std::max(0, m_list->GetSelectedRow());
  const int next =
      forward ? (current + 1) % count : (current - 1 + count) % count;
  m_list->SelectRow(static_cast<unsigned int>(next));
  m_list->EnsureVisible(m_list->RowToItem(next));
}

void PageSwitcherDlg::OnCharHook(wxKeyEvent &event) {
  switch (event.GetKeyCode()) {
  case WXK_TAB:
    Advance(!event.ShiftDown());
    return;
  case WXK_ESCAPE:
    EndModal(wxID_CANCEL);
    return;
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    EndModal(wxID_OK);
    return;
  default:
    event.Skip();
    return;
  }
}

void PageSwitcherDlg::OnKeyUp(wxKeyEvent &event) {
  // WXK_RAW_CONTROL is the physical Ctrl key on every platform (on macOS it is
  // distinct from Cmd).
  if (event.GetKeyCode() == WXK_RAW_CONTROL) {
    EndModal(wxID_OK);
    return;
  }
  event.Skip();
}

void PageSwitcherDlg::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  if (!wxGetMouseState().RawControlDown()) {
    EndModal(wxID_OK);
  }
}

void PageSwitcherDlg::OnActivated(wxDataViewEvent &event) {
  wxUnusedVar(event);
  EndModal(wxID_OK);
}
