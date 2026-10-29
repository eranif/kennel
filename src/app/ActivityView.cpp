#include "app/ActivityView.hpp"

#include "core/AppManager.h"

#include <algorithm>
#include <wx/app.h>
#include <wx/dataview.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(wxEVT_ACTIVITYVIEW_SESSION_SELECTED, PageViewEvent);

ActivityView::ActivityView(wxWindow *parent)
    : wxPanel(parent), m_spinnerTimer(this) {
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
  Bind(wxEVT_TIMER, &ActivityView::OnSpinnerTimer, this,
       m_spinnerTimer.GetId());
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
      RemoveSession(ref);
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

void ActivityView::SetActiveSession(const std::optional<SessionRef> &ref) {
  m_active = ref;
}

void ActivityView::RemoveSession(const SessionRef &ref) {
  const int index = FindRow(ref);
  if (index == wxNOT_FOUND) {
    return;
  }
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
  wxString icon;
  switch (row.activity) {
  case PageActivity::Blocked:
    icon = "hand";
    break;
  case PageActivity::Done:
    icon = "check";
    break;
  default:
    icon = wxString::Format("spinner-%d", m_spinnerFrame);
    break;
  }
  wxVariant cell;
  cell << wxDataViewIconText(wxEmptyString, bitmaps.GetByAlias(icon, false));
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

  m_list->DeleteAllItems();
  bool working = false;
  for (const Row &row : m_rows) {
    working = working || row.activity == PageActivity::Working;
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

  if (working && !m_spinnerTimer.IsRunning()) {
    m_spinnerTimer.Start(kSpinnerMs);
  } else if (!working) {
    m_spinnerTimer.Stop();
  }
}

void ActivityView::OnSpinnerTimer(wxTimerEvent &) {
  // Nobody sees the spinner of a hidden pane or of a window in the background.
  if (!IsShownOnScreen() || !wxTheApp->IsActive()) {
    return;
  }
  m_spinnerFrame = (m_spinnerFrame + 1) % kSpinnerFrameCount;
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
    RemoveSession(*m_active);
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
  ProcessWindowEvent(picked);
  // A working or blocked row stays selected otherwise, and a second click on it
  // sends nothing (the selection does not change).
  CallAfter([this] { m_list->UnselectAll(); });
}
