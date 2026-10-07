#pragma once

#include <wx/dataview.h>
#include <wx/event.h>
#include <wx/icon.h>
#include <wx/string.h>

class wxWindow;

// What a view (TreeView / FlatView) knows about one page of the main view: a
// terminal / agent session (SessionPage) or an open file (FilePage).
struct PageInfo {
  wxWindow *page{nullptr}; // Non-owning: the page lives in MainView's book
  wxString name;           // The leaf / row label
  wxString group;          // The group, or the Terminals / Files container
  wxIcon icon;
};

// Sent by a view to its parent chain (and so to MainView) when the user
// interacts with it:
//   wxEVT_PAGEVIEW_SELECTED: the user picked `GetPage()`.
//   wxEVT_PAGEVIEW_MENU:     the user asked for a context menu on `GetPage()`
//                            (a page), on `GetGroupName()` (a tree group), or
//                            on neither (empty space). The event object is the
//                            view to pop the menu up on.
class PageViewEvent : public wxCommandEvent {
public:
  explicit PageViewEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  PageViewEvent(const PageViewEvent &) = default;

  wxEvent *Clone() const override { return new PageViewEvent(*this); }

  const wxString &GetGroupName() const { return m_groupName; }
  void SetGroupName(const wxString &name) { m_groupName = name; }

  bool UpdateRecent() const { return m_updateRecent; }
  void SetUpdateRecent(bool b) { m_updateRecent = b; }

  void SetSessionName(const wxString &sessionName) {
    this->m_sessionName = sessionName;
  }
  const wxString &GetSessionName() const { return m_sessionName; }

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
