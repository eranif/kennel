#pragma once

#include <wx/dataview.h>
#include <wx/event.h>
#include <wx/string.h>

#include "core/Account.h"
#include "core/Workspace.h"

// What the program of a session reports it is doing (OSC 7501).
enum class PageActivity {
  None,
  Working,
  Done,
  Blocked,
};

// An event about a page (a session); the page is named by GetRef() (a
// SessionRef, see core/Workspace.h) and the event object is the sender.
class PageViewEvent : public wxCommandEvent {
public:
  explicit PageViewEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  PageViewEvent(const PageViewEvent &) = default;

  wxEvent *Clone() const override { return new PageViewEvent(*this); }

  const SessionRef &GetRef() const { return m_ref; }
  void SetRef(const SessionRef &ref) { m_ref = ref; }

  // A rename: the name the page had (GetRef() is the new one).
  const SessionRef &GetOldRef() const { return m_oldRef; }
  void SetOldRef(const SessionRef &ref) { m_oldRef = ref; }

  // The place the files of the session are on.
  const Account &GetAccount() const { return m_account; }
  void SetAccount(const Account &account) { m_account = account; }

  // What the program of the session is doing now.
  PageActivity GetActivity() const { return m_activity; }
  void SetActivity(PageActivity activity) { m_activity = activity; }

private:
  SessionRef m_ref;
  SessionRef m_oldRef;
  Account m_account;
  PageActivity m_activity{PageActivity::None};
};

// The events below are how the pages (MainNotebook) and the rest of the main
// window (MainView) talk: neither knows the other.
//
// From the pages, to the parent windows (the receiver follows and sends
// nothing back):
//   wxEVT_SESSION_PAGE_ADDED:    a session with files to show: GetRef(),
//                                GetAccount().
//   wxEVT_SESSION_PAGE_REMOVED:  GetRef() is gone.
//   wxEVT_SESSION_PAGE_RENAMED:  GetOldRef() is GetRef() now.
//   wxEVT_SESSION_PAGES_CLEARED: every session is gone.
//   wxEVT_SESSION_PAGE_SHOWN:    the session GetRef() is the page showing now.
//   wxEVT_SESSION_PAGE_ACTIVITY: GetActivity() is what the program of GetRef()
//                                is doing now (None: nothing), also for the
//                                page that is showing.
//   wxEVT_PAGES_TOOLBAR_STYLE:   the toolbars take the style of the theme
//   again.
// (A file that is showing is reported with wxEVT_FILE_REVEAL, see FileEvent.)
wxDECLARE_EVENT(wxEVT_SESSION_PAGE_ADDED, PageViewEvent);
wxDECLARE_EVENT(wxEVT_SESSION_PAGE_REMOVED, PageViewEvent);
wxDECLARE_EVENT(wxEVT_SESSION_PAGE_RENAMED, PageViewEvent);
wxDECLARE_EVENT(wxEVT_SESSION_PAGES_CLEARED, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SESSION_PAGE_SHOWN, PageViewEvent);
wxDECLARE_EVENT(wxEVT_SESSION_PAGE_ACTIVITY, PageViewEvent);
wxDECLARE_EVENT(wxEVT_PAGES_TOOLBAR_STYLE, wxCommandEvent);

// Settings of the lists of the pages: alternating row colours (ignored by the
// native implementations).
void StylePageView(wxDataViewCtrl *ctrl);
