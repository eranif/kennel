#include "PageSwitcherDlg.hpp"

#include "app/PageViewEvent.hpp"

#include <wx/dataview.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <algorithm>

wxDEFINE_EVENT(wxEVT_PAGE_SWITCHER_DONE, wxCommandEvent);

namespace {
constexpr int kMaxVisibleRows = 12;
constexpr int kMinWidth = 500;
constexpr int kMinHeight = 300;
constexpr int kMaxWidth = 1000;
constexpr int kPollMs = 30;
constexpr int kIconSize = 16;
#ifdef __WXOSX__
constexpr int kHotKeyNext = 1;
constexpr int kHotKeyPrev = 2;
#endif
} // namespace

PageSwitcherDlg::PageSwitcherDlg(wxWindow *parent, wxEvtHandler *sink,
                                 const std::vector<PageSwitcherItem> &items,
                                 bool forward)
    : wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
               wxDefaultSize, wxBORDER_SIMPLE | wxSTAY_ON_TOP),
      m_sink(sink), m_timer(this) {
  m_list =
      new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxDV_NO_HEADER | wxDV_SINGLE | wxDV_ROW_LINES |
                                 wxDV_VERT_RULES | wxBORDER_NONE);
  StylePageView(m_list);

  int labelWidth = 0;
  int dirWidth = 0;
  int groupWidth = 0;
  for (const auto &item : items) {
    labelWidth = std::max(labelWidth, GetTextExtent(item.label).GetWidth());
    dirWidth = std::max(dirWidth, GetTextExtent(item.dir).GetWidth());
    groupWidth = std::max(groupWidth, GetTextExtent(item.group).GetWidth());
  }
  // The icon and the paddings of the two columns.
  labelWidth += kIconSize + 40;
  dirWidth += 30;
  groupWidth += 30;
  const int width =
      std::clamp(labelWidth + dirWidth + groupWidth + 24, kMinWidth, kMaxWidth);
  // The name and the group keep what they need (up to a share of the width),
  // the folder gets the rest.
  const int nameColumn = std::min(labelWidth, width * 4 / 10);
  const int groupColumn = std::min(groupWidth, width * 2 / 10);
  const int dirColumn = width - 24 - nameColumn - groupColumn;
  const int rowHeight = std::max(m_list->GetCharHeight(), kIconSize) + 10;
  const int rows = std::min(static_cast<int>(items.size()), kMaxVisibleRows);

  m_list->AppendIconTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT,
                               nameColumn);
  m_list->AppendTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT, dirColumn);
  m_list->AppendTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT, groupColumn);
  for (const auto &item : items) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(wxDataViewIconText(item.label, item.icon)));
    row.push_back(wxVariant(item.dir));
    row.push_back(wxVariant(item.group));
    m_list->AppendItem(row);
  }
  m_list->SetMinSize(wxSize(width, std::max(rows * rowHeight, kMinHeight)));

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand().Border(wxALL, 1));
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
  CentreOnParent();
  Show();
  m_list->SetFocus();
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
    Finish(false);
    return;
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    Finish(true);
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
    Finish(true);
    return;
  }
  event.Skip();
}

void PageSwitcherDlg::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  if (!wxGetMouseState().RawControlDown()) {
    Finish(true);
  }
}

void PageSwitcherDlg::OnActivated(wxDataViewEvent &event) {
  wxUnusedVar(event);
  Finish(true);
}

void PageSwitcherDlg::Finish(bool accept) {
  if (m_done) {
    return;
  }
  m_done = true;
  m_timer.Stop();
  Hide();
  wxCommandEvent evt(wxEVT_PAGE_SWITCHER_DONE);
  evt.SetInt(accept ? GetSelectedIndex() : wxNOT_FOUND);
  m_sink->QueueEvent(evt.Clone());
}
