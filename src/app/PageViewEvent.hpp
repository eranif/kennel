#pragma once

#include <wx/dataview.h>
#include <wx/event.h>
#include <wx/icon.h>
#include <wx/string.h>

// Names one page of the main view. Pages are never named by pointer.
//   A session (or a plain terminal): `name` is the session name, `group` its
//   group.
//   A file: `name` is the file key (FilePage::GetKey()), `group` is the Files
//   container.
struct GroupAndName {
  wxString name;
  wxString group;

  bool operator==(const GroupAndName &) const = default;
};

// What a view (TreeView / FlatView) shows for one page of the main view: a
// terminal / agent session (SessionPage) or an open file (FilePage).
struct PageInfo {
  wxString label; // The leaf / row label
  wxString group; // The group, or the Terminals / Files container
  wxString key;   // The session name, or the file key for a file
  wxIcon icon;

  GroupAndName Ref() const { return GroupAndName{key, group}; }
};

// Sent when the user interacts with a view. The page is named by
// GetGroupName() + GetSessionName() (see GroupAndName); the event object is the
// sender.
//   wxEVT_PAGEVIEW_SELECTED: the user picked a page. Sent through
//                            EventNotifier; MainView shows the page and every
//                            view other than the sender selects it too.
//                            MainView sends it as well when it shows a page
//                            itself, so the views follow.
//   wxEVT_PAGEVIEW_MENU:     the user asked for a context menu on a page
//                            (GetSessionName() set), on a tree group (only
//                            GetGroupName() set), or on empty space (neither).
//                            Sent to the parent chain (MainView); the event
//                            object is the view to pop the menu up on.
class PageViewEvent : public wxCommandEvent {
public:
  explicit PageViewEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  PageViewEvent(const PageViewEvent &) = default;

  wxEvent *Clone() const override { return new PageViewEvent(*this); }

  const wxString &GetGroupName() const { return m_groupName; }
  void SetGroupName(const wxString &name) { m_groupName = name; }

  // Whether showing the page makes it the most recently used one (the
  // Ctrl+Tab order). False for a pick in the flat list.
  bool UpdateRecent() const { return m_updateRecent; }
  void SetUpdateRecent(bool b) { m_updateRecent = b; }

  void SetSessionName(const wxString &sessionName) {
    this->m_sessionName = sessionName;
  }
  const wxString &GetSessionName() const { return m_sessionName; }

  GroupAndName GetRef() const {
    return GroupAndName{m_sessionName, m_groupName};
  }
  void SetRef(const GroupAndName &ref) {
    m_sessionName = ref.name;
    m_groupName = ref.group;
  }

private:
  wxString m_groupName;
  wxString m_sessionName;
  bool m_updateRecent{false};
};

wxDECLARE_EVENT(wxEVT_PAGEVIEW_SELECTED, PageViewEvent);
wxDECLARE_EVENT(wxEVT_PAGEVIEW_MENU, PageViewEvent);

// Settings shared by the tree and the flat list: alternating row colours
// (ignored by the native implementations).
void StylePageView(wxDataViewCtrl *ctrl);

// Adds `child` to `host`, filling it. Uses the sizer wxCrafter gave `host`, or
// creates one.
void FillPanel(wxWindow *host, wxWindow *child);
