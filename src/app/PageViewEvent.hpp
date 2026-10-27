#pragma once

#include <wx/bmpbndl.h>
#include <wx/dataview.h>
#include <wx/event.h>
#include <wx/string.h>

#include "core/Workspace.h"

// What a view (FlatView) shows for one page of the main view: a terminal /
// agent session (SessionPage) or an open file (FilePage).
struct PageInfo {
  wxString label;      // The row label
  wxString group;      // The group, or the Terminals / Files container
  wxString key;        // The session name, or the file key for a file
  wxString dir;        // The folder of a terminal (empty for the others)
  wxBitmapBundle icon; // A bundle, not an icon: it keeps its size on any DPI
  wxBitmapBundle groupIcon; // The icon of the group
  // What the program in the page is doing now: the spinner, the check mark or
  // the hand. Not ok when it does nothing.
  wxBitmapBundle statusIcon;

  SessionRef Ref() const { return SessionRef{group, key}; }
  // The status when there is one, otherwise the icon of the page.
  const wxBitmapBundle &DisplayIcon() const {
    return statusIcon.IsOk() ? statusIcon : icon;
  }
};

// Sent when the user interacts with a view. The page is named by GetRef() (a
// SessionRef, see core/Workspace.h); the event object is the sender.
//   wxEVT_PAGEVIEW_SELECTED: the user picked a page. Sent through
//                            EventNotifier; MainView shows the page and every
//                            view other than the sender selects it too.
//                            MainView sends it as well when it shows a page
//                            itself, so the views follow.
//   wxEVT_PAGEVIEW_MENU:     the user asked for a context menu on a page
//                            (GetSessionName() set) or on empty space (not
//                            set). Sent to the parent chain (MainView); the
//                            event object is the view to pop the menu up on.
class PageViewEvent : public wxCommandEvent {
public:
  explicit PageViewEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  PageViewEvent(const PageViewEvent &) = default;

  wxEvent *Clone() const override { return new PageViewEvent(*this); }

  // Whether showing the page makes it the most recently used one (the
  // Ctrl+Tab order). False for a pick in the flat list.
  bool UpdateRecent() const { return m_updateRecent; }
  void SetUpdateRecent(bool b) { m_updateRecent = b; }

  void SetSessionName(const wxString &sessionName) {
    this->m_sessionName = sessionName;
  }
  const wxString &GetSessionName() const { return m_sessionName; }

  SessionRef GetRef() const { return SessionRef{m_groupName, m_sessionName}; }
  void SetRef(const SessionRef &ref) {
    m_sessionName = ref.name;
    m_groupName = ref.groupName;
  }

private:
  wxString m_groupName;
  wxString m_sessionName;
  bool m_updateRecent{false};
};

wxDECLARE_EVENT(wxEVT_PAGEVIEW_SELECTED, PageViewEvent);
wxDECLARE_EVENT(wxEVT_PAGEVIEW_MENU, PageViewEvent);

// Settings of the flat list: alternating row colours (ignored by the native
// implementations).
void StylePageView(wxDataViewCtrl *ctrl);

// Adds `child` to `host`, filling it. Uses the sizer wxCrafter gave `host`, or
// creates one.
void FillPanel(wxWindow *host, wxWindow *child);
