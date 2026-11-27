#pragma once

#include <wx/event.h>
#include <wx/string.h>

#include "core/Account.h"
#include "core/FilePageUID.h"
#include "core/Workspace.h"

// What the program of a session reports it is doing (OSC 7501).
enum class PageActivity {
  None,
  Working,
  Done,
  Blocked,
};

// What a page is.
enum class PageType {
  kFile,     // A file
  kSession,  // A session of an agent
  kTerminal, // A terminal (a session without an agent)
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

  // The folder the page works in: the working directory of a session or a
  // terminal, in the notation of GetAccount() (a Linux path for WSL and for a
  // remote host). For a file: the folder part of GetPath().
  const wxString &GetWorkingDir() const { return m_workingDir; }
  void SetWorkingDir(const wxString &dir) { m_workingDir = dir; }

  // What the page is.
  PageType GetPageType() const { return m_pageType; }
  void SetPageType(PageType type) { m_pageType = type; }

  // A file: its full path in the notation of GetAccount() ("C:\a\b\file.cpp",
  // "/home/me/file.cpp"; a Linux path for WSL and for a remote host). Empty for
  // the other pages.
  const wxString &GetPath() const { return m_path; }
  void SetPath(const wxString &path) { m_path = path; }

  // A file that was renamed (Save As, or an untitled file that was saved): the
  // path it had, in the same notation as GetPath(), which is the new one.
  const wxString &GetOldFilePath() const { return m_oldFilePath; }
  void SetOldFilePath(const wxString &path) { m_oldFilePath = path; }
  // A file: the ID of its editor (see IEditor::GetEditorUID()); zero for the
  // other pages.
  FilePageUID GetEditorUID() const { return m_editorUID; }
  void SetEditorUID(FilePageUID uid) { m_editorUID = uid; }

private:
  wxString m_path;
  wxString m_oldFilePath;
  FilePageUID m_editorUID{0};
  SessionRef m_ref;
  SessionRef m_oldRef;
  Account m_account;
  PageActivity m_activity{PageActivity::None};
  wxString m_workingDir;
  PageType m_pageType{PageType::kSession};
};

// The events below are how the pages (MainNotebook) and the side views
// (ActivityView, SideView) talk: neither knows the other. They travel
// on the EventNotifier.
//
// From the pages, on the EventNotifier (the receiver follows and sends nothing
// back):
//   wxEVT_PAGE_ADDED:    a session or a terminal with files to show, or a file:
//                                GetRef() (not for a file), GetAccount(),
//                                GetWorkingDir(), GetPageType(), GetPath()
//                                (a file only).
//   wxEVT_PAGE_REMOVED:  GetRef() is gone (it is still there while the
//                                event is handled): GetAccount(),
//                                GetWorkingDir(), GetPageType(), GetPath() (a
//                                file only).
//   wxEVT_PAGE_RENAMED:  GetOldRef() is GetRef() now. A file (GetPageType() is
//                                kFile): GetOldFilePath() is GetPath() now,
//                                with GetAccount() and GetWorkingDir() of the
//                                new path.
//   wxEVT_SESSION_PAGES_CLEARED: every session is gone.
//   wxEVT_PAGE_SHOWN:    the session GetRef() is the page showing now.
//   wxEVT_PAGE_MOVED:    the place of a terminal changed (it went to
//                                another folder): GetRef(), GetAccount().
//                                Only plain terminals send it.
//   wxEVT_PAGE_ACTIVITY: GetActivity() is what the program of GetRef()
//                                is doing now (None: nothing), also for the
//                                page that is showing.
//   wxEVT_PAGES_TOOLBAR_STYLE:   the toolbars take the style of the theme
//   again.
// From the side bar (SideView), on the EventNotifier:
//   wxEVT_SESSION_SIDE_DIR_CHANGED: the user chose another folder for the files
//                                of the session GetRef():
//                                GetAccount().m_rootDir is the folder. The page
//                                keeps it in its session (it is saved), and
//                                wxEVT_PAGE_ADDED has it the next time.
//                                Never sent for a terminal (it follows its
//                                shell).
// (A file that is showing is reported with wxEVT_FILE_REVEAL, see FileEvent.)
wxDECLARE_EVENT(wxEVT_PAGE_ADDED, PageViewEvent);
wxDECLARE_EVENT(wxEVT_PAGE_REMOVED, PageViewEvent);
wxDECLARE_EVENT(wxEVT_PAGE_RENAMED, PageViewEvent);
wxDECLARE_EVENT(wxEVT_SESSION_PAGES_CLEARED, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_PAGE_SHOWN, PageViewEvent);
wxDECLARE_EVENT(wxEVT_PAGE_ACTIVITY, PageViewEvent);
wxDECLARE_EVENT(wxEVT_PAGE_MOVED, PageViewEvent);
wxDECLARE_EVENT(wxEVT_PAGES_TOOLBAR_STYLE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SESSION_SIDE_DIR_CHANGED, PageViewEvent);

// Sent on the EventNotifier with ProcessEvent() (so the answer is known when it
// returns) when the application is closing. A listener that needs the user (a
// file with changes asks Save / Discard / Cancel) does it, and calls Veto()
// when the application must not close. CanVeto() is false when the system is
// ending the session: nobody can be asked then.
wxDECLARE_EVENT(wxEVT_APP_CLOSING, wxCloseEvent);
