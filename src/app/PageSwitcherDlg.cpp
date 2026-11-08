#include "PageSwitcherDlg.hpp"

#include "app/DataViewUtils.hpp"
#include "app/PopupStyle.hpp"
#include <wx/dataview.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <algorithm>

wxDEFINE_EVENT(wxEVT_PAGE_SWITCHER_DONE, wxCommandEvent);

namespace {
constexpr int kMaxVisibleRows = 12;
constexpr int kMinWidth = 400;
constexpr int kMinHeight = 300;
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
    : wxPopupTransientWindow(parent, wxBORDER_DEFAULT), m_sink(sink),
      m_timer(this) {
  m_list =
      new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxDV_NO_HEADER | wxDV_SINGLE | wxDV_VERT_RULES |
                                 wxWANTS_CHARS | wxBORDER_NONE);
  dv::StylePageView(m_list);

  int labelWidth = 0;
  int groupWidth = 0;
  for (const auto &item : items) {
    labelWidth = std::max(labelWidth, GetTextExtent(item.label).GetWidth());
    groupWidth = std::max(groupWidth, GetTextExtent(item.group).GetWidth());
  }
  // The icon and the padding of the columns.
  labelWidth += kIconSize + 40;
  groupWidth += 30;
  // A third of the width of the application.
  const int width = std::max(parent->GetClientSize().x / 3, kMinWidth);
  // The name and the group keep what they need (up to a share of the width),
  // the folder gets the rest.
  const int nameColumn = std::min(labelWidth, width * 4 / 10);
  const int groupColumn = std::min(groupWidth, width * 2 / 10);
  const int dirColumn = width - 24 - nameColumn - groupColumn;
  const int rowHeight = std::max(m_list->GetCharHeight(), kIconSize) + 10;
  const int rows = std::min(static_cast<int>(items.size()), kMaxVisibleRows);

  m_list->AppendIconTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT,
                               nameColumn);
  m_list->AppendTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT, groupColumn);
  m_list->AppendTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT, dirColumn);
  for (const auto &item : items) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(wxDataViewIconText(item.label, item.icon)));
    row.push_back(wxVariant(item.group));
    row.push_back(wxVariant(item.dir));
    m_list->AppendItem(row);
  }

  m_list->SetMinSize(wxSize(width, std::max(rows * rowHeight, kMinHeight)));
  dv::AutoSizeColumns(m_list);

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand().Border(wxALL, 1));
  SetSizerAndFit(sizer);

  Bind(wxEVT_TIMER, &PageSwitcherDlg::OnTimer, this);
  m_list->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &PageSwitcherDlg::OnActivated,
               this);

  const int count = static_cast<int>(items.size());
  if (count > 0) {
    const int initial = forward ? std::min(1, count - 1) : count - 1;
    m_list->SelectRow(static_cast<unsigned int>(initial));
    m_list->EnsureVisible(m_list->RowToItem(initial));
  }
#ifdef __WXOSX__
  // macOS uses Ctrl+Tab to move the keyboard focus before the application gets
  // the key, so the key filter never sees it. A hot key is caught before that.
  // It is unregistered in the destructor.
  RegisterHotKey(kHotKeyNext, wxMOD_RAW_CONTROL, WXK_TAB);
  RegisterHotKey(kHotKeyPrev, wxMOD_RAW_CONTROL | wxMOD_SHIFT, WXK_TAB);
  Bind(wxEVT_HOTKEY, [this](wxKeyEvent &) { Advance(true); }, kHotKeyNext);
  Bind(wxEVT_HOTKEY, [this](wxKeyEvent &) { Advance(false); }, kHotKeyPrev);
#endif
  wxEvtHandler::AddFilter(this);

  // At the top of the application's client area, centred horizontally,
  // 1/2 of its width and 1/4 of its height (screen coordinates).
  const wxRect parentRect(parent->ClientToScreen(wxPoint(0, 0)),
                          parent->GetClientSize());
  wxRect ourRect(0, 0, parentRect.GetWidth() / 2, parentRect.GetHeight() / 4);
  ourRect = ourRect.CenterIn(parentRect, wxHORIZONTAL);
  ourRect.SetY(parentRect.GetY());
  SetSize(ourRect);

  Popup(m_list);
  // A shadow and rounded corners (macOS, Windows), after the window exists.
  StylePopupWindow(this);

  // Releasing Ctrl normally arrives as a key-up that the filter sees, but poll
  // the modifier state too in case the release happened before we were shown.
  m_timer.Start(kPollMs);
}

PageSwitcherDlg::~PageSwitcherDlg() {
  m_timer.Stop();
  wxEvtHandler::RemoveFilter(this);
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

int PageSwitcherDlg::FilterEvent(wxEvent &event) {
  if (m_done) {
    return Event_Skip;
  }
  const auto type = event.GetEventType();
  if (type != wxEVT_KEY_UP && type != wxEVT_CHAR_HOOK) {
    return Event_Skip;
  }
  auto &key = static_cast<wxKeyEvent &>(event);
  if (type == wxEVT_KEY_UP) {
    // WXK_RAW_CONTROL is the physical Ctrl key on every platform (on macOS it
    // is distinct from Cmd).
    if (key.GetKeyCode() == WXK_RAW_CONTROL) {
      Finish(true);
    }
    return Event_Skip;
  }
  switch (key.GetKeyCode()) {
  case WXK_TAB:
    Advance(!key.ShiftDown());
    return Event_Processed;
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    Finish(true);
    return Event_Processed;
  case WXK_ESCAPE:
    Finish(false);
    return Event_Processed;
  default:
    return Event_Skip;
  }
}

void PageSwitcherDlg::OnTimer(wxTimerEvent &) {
  if (!m_done && !wxGetMouseState().RawControlDown()) {
    Finish(true);
  }
}

void PageSwitcherDlg::OnActivated(wxDataViewEvent &) { Finish(true); }

void PageSwitcherDlg::Finish(bool accept) {
  if (m_done) {
    return;
  }
  m_done = true;
  m_timer.Stop();
  m_result = accept ? GetSelectedIndex() : wxNOT_FOUND;
  // Hides the popup and calls OnDismiss(), which reports the result.
  DismissAndNotify();
}

void PageSwitcherDlg::OnDismiss() {
  // Also called for a click outside of the popup: Finish() was not called then,
  // and the result stays "cancelled".
  m_done = true;
  m_timer.Stop();
  wxCommandEvent evt(wxEVT_PAGE_SWITCHER_DONE);
  evt.SetInt(m_result);
  m_sink->QueueEvent(evt.Clone());
  // Destroy later: we may be inside one of our own event handlers. The event
  // above is queued first, so the receiver forgets us before we are gone.
  CallAfter([this] { Destroy(); });
}
