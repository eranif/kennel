#include "app/ActivityView.hpp"

#include "core/AppManager.h"

#include "core/EventNotifier.hpp"
#include <algorithm>
#include <wx/app.h>
#include <wx/dataview.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(wxEVT_ACTIVITYVIEW_SESSION_SELECTED, PageViewEvent);

ActivityView::ActivityView(wxWindow *parent)
    : wxPanel(parent),
      m_spinner([this](const wxBitmapBundle &) { OnSpinnerFrame(); }) {
  m_list = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxDV_SINGLE | wxDV_ROW_LINES);
  StylePageView(m_list);
  // The status is the icon only, in a column of a fixed width (flags 0: the
  // user cannot resize it). The other columns fit their content (see Refill()).
  m_list->AppendIconTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT,
                               FromDIP(kStatusColumnWidthDip), wxALIGN_CENTER,
                               0);
  m_list->AppendTextColumn(_("Session"), wxDATAVIEW_CELL_INERT, 140);
  m_list->AppendTextColumn(_("Group"), wxDATAVIEW_CELL_INERT, 100);

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  m_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED,
               &ActivityView::OnSelectionChanged, this);
  Bind(wxEVT_IDLE, &ActivityView::OnIdle, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGE_RENAMED,
                             &ActivityView::OnSessionRenamed, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGE_REMOVED,
                             &ActivityView::OnSessionPageRemoved, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGES_CLEARED,
                             &ActivityView::OnSessionPagesCleared, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGE_SHOWN,
                             &ActivityView::OnSessionPageShown, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGE_ACTIVITY,
                             &ActivityView::OnSessionPageActivity, this);
  EventNotifier::Get()->Bind(wxEVT_FILE_REVEAL, &ActivityView::OnFileReveal,
                             this);
}

ActivityView::~ActivityView() {
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGE_RENAMED,
                               &ActivityView::OnSessionRenamed, this);
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGE_REMOVED,
                               &ActivityView::OnSessionPageRemoved, this);
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGES_CLEARED,
                               &ActivityView::OnSessionPagesCleared, this);
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGE_SHOWN,
                               &ActivityView::OnSessionPageShown, this);
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGE_ACTIVITY,
                               &ActivityView::OnSessionPageActivity, this);
  EventNotifier::Get()->Unbind(wxEVT_FILE_REVEAL, &ActivityView::OnFileReveal,
                               this);
}

int ActivityView::FindRow(const SessionRef &ref) const {
  for (size_t i = 0; i < m_rows.size(); ++i) {
    if (m_rows[i].ref == ref) {
      return static_cast<int>(i);
    }
  }
  return wxNOT_FOUND;
}

void ActivityView::SetActivity(const SessionRef &ref, PageActivity activity) {
  if (activity == PageActivity::None) {
    // A done goes when the session is the active one (see OnIdle()), even when
    // the program is idle again.
    if (const int index = FindRow(ref);
        index != wxNOT_FOUND && m_rows[index].activity != PageActivity::Done) {
      RemoveRow(ref);
    }
    return;
  }
  if (const int index = FindRow(ref); index != wxNOT_FOUND) {
    if (m_rows[index].activity == activity) {
      return;
    }
    m_rows[index].activity = activity;
    m_rows[index].serial = ++m_serial;
  } else {
    m_rows.push_back({ref, activity, ++m_serial});
  }
  Refill();
}

void ActivityView::RemoveRow(const SessionRef &ref) {
  const int index = FindRow(ref);
  if (index == wxNOT_FOUND) {
    return;
  }
  // Only the row: the session may be the one the user works in (m_active), and
  // goes on being it.
  m_rows.erase(m_rows.begin() + index);
  Refill();
}

void ActivityView::RenameSession(const SessionRef &from, const SessionRef &to) {
  const int index = FindRow(from);
  if (index == wxNOT_FOUND) {
    return;
  }
  m_rows[index].ref = to;
  if (m_active == from) {
    m_active = to;
  }
  Refill();
}

void ActivityView::Clear() {
  m_rows.clear();
  m_active.reset();
  Refill();
}

wxVariant ActivityView::StatusCell(const Row &row) const {
  const auto &bitmaps = AppManager::Get().GetBitmaps();
  wxBitmapBundle icon;
  switch (row.activity) {
  case PageActivity::Blocked:
    icon = bitmaps.GetByAlias("hand", false);
    break;
  case PageActivity::Done:
    icon = bitmaps.GetByAlias("check", false);
    break;
  default:
    icon = m_spinner.GetBitmap();
    break;
  }
  wxVariant cell;
  cell << wxDataViewIconText(wxEmptyString, icon);
  return cell;
}

void ActivityView::Refill() {
  // What needs the user first: Blocked, then Done, then Working.
  const auto rank = [](PageActivity activity) {
    switch (activity) {
    case PageActivity::Blocked:
      return 0;
    case PageActivity::Done:
      return 1;
    default:
      return 2;
    }
  };
  std::stable_sort(m_rows.begin(), m_rows.end(),
                   [&rank](const Row &a, const Row &b) {
                     if (rank(a.activity) != rank(b.activity)) {
                       return rank(a.activity) < rank(b.activity);
                     }
                     return a.serial > b.serial;
                   });

  // Before the cells: the spinner has its frames when it starts.
  const bool working =
      std::any_of(m_rows.begin(), m_rows.end(), [](const Row &row) {
        return row.activity == PageActivity::Working;
      });
  if (working) {
    m_spinner.Start();
  } else {
    m_spinner.Stop();
  }

  m_list->DeleteAllItems();
  for (const Row &row : m_rows) {
    wxVector<wxVariant> cells;
    cells.push_back(StatusCell(row));
    cells.push_back(wxVariant(row.ref.name));
    cells.push_back(wxVariant(row.ref.groupName));
    m_list->AppendItem(cells);
  }
  // The columns fit the rows that are there now.
  for (unsigned int column = kFirstFitColumn; column < m_list->GetColumnCount();
       ++column) {
    m_list->GetColumn(column)->SetWidth(wxCOL_WIDTH_AUTOSIZE);
  }
}

void ActivityView::OnSpinnerFrame() {
  // Nobody sees the spinner of a hidden pane or of a window in the background.
  if (!IsShownOnScreen() || !wxTheApp->IsActive()) {
    return;
  }
  for (size_t i = 0; i < m_rows.size(); ++i) {
    if (m_rows[i].activity == PageActivity::Working) {
      m_list->SetValue(StatusCell(m_rows[i]), static_cast<unsigned>(i),
                       kStatusColumn);
    }
  }
}

void ActivityView::OnIdle(wxIdleEvent &event) {
  event.Skip();
  // Nobody sees a done in a window that is in the background.
  if (!m_active || !wxTheApp->IsActive()) {
    return;
  }
  if (const int index = FindRow(*m_active);
      index != wxNOT_FOUND && m_rows[index].activity == PageActivity::Done) {
    RemoveRow(*m_active);
  }
}

void ActivityView::OnSelectionChanged(wxDataViewEvent &event) {
  event.Skip();
  if (!event.GetItem().IsOk()) {
    return;
  }
  const int index = m_list->ItemToRow(event.GetItem());
  if (index < 0 || index >= static_cast<int>(m_rows.size())) {
    return;
  }
  // The host shows the session.
  PageViewEvent picked(wxEVT_ACTIVITYVIEW_SESSION_SELECTED);
  picked.SetEventObject(this);
  picked.SetRef(m_rows[index].ref);
  picked.SetActivity(m_rows[index].activity);
  EventNotifier::Get()->AddPendingEvent(picked);
  // A working or blocked row stays selected otherwise, and a second click on it
  // sends nothing (the selection does not change).
  CallAfter([this] { m_list->UnselectAll(); });
}

void ActivityView::OnSessionPageShown(PageViewEvent &event) {
  event.Skip();
  m_active = event.GetRef();
}

void ActivityView::OnSessionRenamed(PageViewEvent &event) {
  event.Skip();
  RenameSession(event.GetOldRef(), event.GetRef());
}

void ActivityView::OnSessionPageRemoved(PageViewEvent &event) {
  event.Skip();
  if (m_active == event.GetRef()) {
    m_active.reset(); // The session is gone
  }
  RemoveRow(event.GetRef());
}

void ActivityView::OnSessionPageActivity(PageViewEvent &event) {
  event.Skip();
  SetActivity(event.GetRef(), event.GetActivity());
}

void ActivityView::OnFileReveal(FileEvent &event) {
  event.Skip();

  // Not a session - clear the active session.
  m_active.reset();
}
