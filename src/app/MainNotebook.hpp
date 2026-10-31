#pragma once
#include "GlyphBook.h"
#include "app/FileEvent.hpp"
#include "app/FileViewState.hpp"
#include "app/PageSwitcherDlg.hpp"
#include "app/PageViewEvent.hpp"
#include "core/AppPaths.h"
#include "core/FileLocation.h"
#include "core/Job.h"
#include "core/Spinner.h"
#include "core/Workspace.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>
#include <wx/arrstr.h>
#include <wx/aui/auibook.h>
#include <wx/bmpbndl.h>
#include <wx/clntdata.h>
#include <wx/panel.h>
#include <wx/timer.h>

class AdapterRegistry;
class FilePage;
class GotoAnythingBar;
class SessionPage;
class WorkspaceStore;

// Outcome of a background SFTP read, handed from the worker thread to the UI
// thread by value.
struct RemoteReadResult {
  bool ok{false};
  wxString error;               // Set when !ok
  RemoteHostDetails remoteHost; // Where it was read from...
  wxString clickedPath; // ...and the path as it appeared in the terminal
  wxString path;        // The resolved absolute path; set when ok
  std::string content;  // The file's bytes; set when ok
};

// The pages of the main window, one tab each: a SessionPage (an agent or a
// terminal) or a FilePage (an open file). The notebook is the only source of
// truth for which pages and groups exist (a group is the name the sessions in
// it share; the "Default" group always exists), and it does everything that
// has to do with them: launching and closing, opening files, the recent order,
// the workspace file, the theme.
//
// It does not know the rest of the window. It tells what happens to the
// sessions with the events of PageViewEvent.hpp (and wxEVT_FILE_REVEAL) on the
// EventNotifier, and it listens there to what the side views ask for
// (wxEVT_ACTIVITYVIEW_SESSION_SELECTED, wxEVT_FILETREE_...).
class MainNotebook : public wxPanel {
public:
  explicit MainNotebook(wxWindow *parent, wxWindowID winid = wxID_ANY,
                        const wxPoint &pos = wxDefaultPosition,
                        const wxSize &size = wxDefaultSize,
                        long style = wxTAB_TRAVERSAL | wxNO_BORDER,
                        const wxString &name = wxASCII_STR(wxPanelNameStr));
  ~MainNotebook() override;

  // ---- Launching ---------------------------------------------------------
  // `selectAfterLaunch` false keeps the current focus/selection untouched
  // (used for job runs, which shouldn't steal focus from whatever the user
  // is doing when the timer fires or "Run Now" is clicked).
  bool LaunchSession(const NewSessionRequest &req,
                     bool selectAfterLaunch = true);

  // Shows the Start Agent dialog, then launches on OK. `agentName` preselects
  // an agent (empty -> first defined agent); `groupName` pre-sets the group
  // field (empty -> dialog default, "Default").
  void StartAgent(const wxString &agentName = wxEmptyString,
                  const wxString &groupName = wxEmptyString);

  // Shows a plain terminal.
  void StartTerminal();

  // Launches a one-shot session running `job`'s command/prompt, under a
  // "Jobs" group. Never persisted to workspace.json. `selectAfterLaunch`
  // false (the scheduler's default) keeps focus on whatever the user is
  // doing; pass true (e.g. "Run Now") to switch to and focus the new tab.
  void RunJob(const JobDef &job, bool selectAfterLaunch = false);

  // Rebuilds the pages from the sessions and files persisted in
  // workspace.json. Unsaved edits of a file are not kept. Remote files come
  // back a moment later (they are fetched in the background, so they are not in
  // the order of the recent pages yet: the page used last is only shown if it
  // is a session or a local file).
  void RestoreSessions();

  // ---- Theme and prefs ---------------------------------------------------
  void ApplyPrefs();
  void ApplyTheme(const wxString &themeName);
  void ApplyOptimizedDrawing();
  void ApplyFont(const wxFont &f);

  // ---- Sessions ----------------------------------------------------------
  bool CanRefreshCurrent() const;
  void RefreshCurrentSelection();
  // Shows the Ctrl+Tab page switcher (every open session and file, most
  // recently used first) and activates the page chosen when Ctrl is released.
  // If Ctrl is already up, activates the next/previous page directly.
  void SwitchPage(bool forward);
  size_t SessionCount() const;
  // Sessions and files.
  size_t PageCount() const;
  // Prompts for confirmation, then closes every session in every group.
  void CloseAllSessions();
  // Renames the session that is showing.
  void RenameItem();
  // Whether `groupName` already contains a session named `name`. Names only
  // need to be unique within a group, not across the whole workspace.
  bool IsNameExist(const wxString &name, const wxString &groupName) const;
  // The group of the session that is showing; empty for a file.
  wxString GetSelectedGroupName() const;
  bool IsEmpty() const;
  // The single SessionPage currently shown, or nullptr.
  SessionPage *GetActiveSessionPage() const;
  // The single FilePage currently shown, or nullptr.
  FilePage *GetActiveFilePage() const;
  // The name of the page (session or file) shown, if any.
  std::optional<SessionRef> GetActivePageRef() const;
  // Closes the session; the notebook may be asked from a handler of the page.
  void CloseSession(const wxString &groupName, const wxString &sessionName);

  // ---- Files -------------------------------------------------------------
  // Go to a line of the file that is showing: opens the box on top of the page
  // with ":" in it (GotoAnythingBar); the caret follows what the user types.
  // CanGotoLine() is for the menu item and the key.
  void GotoLine();
  bool CanGotoLine() const;

  // Opens a local file in an editor page (or reselects it if it is already
  // open). `restoring` (the file was open in the last run) keeps it in the
  // background and only logs a failure.
  void OpenLocalFile(const wxString &path, bool restoring = false);
  // Opens the local file `where.path` (like OpenLocalFile()) and puts the caret
  // at `where.line` and `where.column`, both counted from 1. No line (0 or
  // less): the file is only opened.
  void OpenFileAt(const FileLocation &where);

  // Reads `clickedPath` (resolved against `searchDirs`) from `remoteHost` on
  // a worker thread, then shows it via ShowRemoteFile(). While the same file is
  // already being fetched the call starts no second fetch; if that fetch is the
  // restore of the file, the file is selected when it arrives.
  // `line` and `column` (from 1; no line: 0) say where the caret goes once the
  // file is shown.
  void OpenRemoteFile(const RemoteHostDetails &remoteHost,
                      const wxString &clickedPath,
                      const std::vector<wxString> &searchDirs, int line = 0,
                      int column = 0);

  // Shows `text`, fetched from `path` on `remoteHost`, in an editor page.
  // Saving writes it back over SFTP. An already-open page is refreshed, unless
  // it has unsaved changes.
  // `reload` is for a page that asked for the new copy: it replaces unsaved
  // edits too (the user agreed), and the page is not selected.
  // `restoring` (the file was open in the last run) does not select the page
  // either.
  void ShowRemoteFile(const RemoteHostDetails &remoteHost, const wxString &path,
                      const wxString &text, bool editable, bool reload = false,
                      bool restoring = false);

  // Fetches `path` again from `remoteHost` and replaces the text of its open
  // page with it, edits included. The caller has asked the user already.
  void ReloadRemoteFile(const RemoteHostDetails &remoteHost,
                        const wxString &path);

  void CloseFileByKey(const wxString &key);
  // Closes `page` (offering to save unsaved changes) and removes it.
  void CloseFile(FilePage *page);

  // ---- Pages -------------------------------------------------------------
  size_t GetPageCount() const;
  // The page that is showing, or null.
  wxWindow *GetCurrentPage() const;
  std::vector<wxWindow *> GetPages() const;
  std::vector<SessionPage *> GetSessions() const;
  std::vector<SessionPage *> GetSessions(const wxString &group) const;
  std::vector<FilePage *> GetFiles() const;
  SessionPage *FindSession(const wxString &group, const wxString &name) const;
  // The first session called `name`, whatever its group.
  SessionPage *FindSessionByName(const wxString &name) const;
  FilePage *FindFile(const wxString &key) const;
  // The session or file `ref` names, or null.
  wxWindow *FindPage(const SessionRef &ref) const;
  static std::optional<SessionRef> RefOf(wxWindow *page);
  // The groups of agent sessions: "Default" first, then in the order of the
  // tabs.
  wxArrayString GetGroupNames() const;

  // Makes `page` (a SessionPage or a FilePage) the one visible page. It also
  // becomes the most recently used one, unless `updateRecent` is false. Pages
  // that are not in the notebook are ignored.
  void ShowPage(wxWindow *page, bool updateRecent = true);

  // ---- Find ----------------------------------------------------------------
  // Find in the page that is showing: a file (its bar also replaces) or a
  // session. CanFind() is for the menu items.
  void ShowFind();
  void FindNext(bool forward);
  bool CanFind() const;
  // Whether F3 / Shift+F3 are Find Next / Previous now: in a file, or in a
  // session that has a search. Otherwise a program in the terminal gets F3.
  bool CanFindNext() const;

private:
  // ---- The tabs ------------------------------------------------------------
  // The window to create a page with: pages are children of the book.
  wxBookCtrlBase *GetPageParent() const { return m_book; }
  // Adds a tab for `page` (not selected). The label and the icon of the tab
  // are the page's.
  void AddTab(wxWindow *page);
  // Removes the tab of `page` and destroys it.
  void DeleteTab(wxWindow *page);
  void DeleteAllTabs();
  // Shows the tab of `page`. Only the user's clicks send events, this does not.
  void SelectTab(wxWindow *page);
  void SetTabTitle(wxWindow *page, const wxString &title);
  // What the program in the session is doing (the spinner, the check mark, the
  // hand) is the icon of the tab; an icon that is not ok shows the session's
  // own icon.
  void SetStatusIcon(SessionPage *page, const wxBitmapBundle &status);
  // The page that is showing after the one that was showing is gone: the one
  // used most recently, but not one of `closing` (null without such a page).
  wxWindow *GetFallbackPage(std::span<wxWindow *const> closing = {}) const;
  // What the page switcher shows for `page`: the label, the icon, the folder.
  PageSwitcherItem DescribePage(wxWindow *page) const;

  // The user clicked a tab: its page is shown.
  void OnPageChanged(wxAuiNotebookEvent &event);
  // The user clicked the close button of a tab: its page is closed.
  void OnPageClose(wxAuiNotebookEvent &event);
  // The focus went into a page: nothing is done (see the constructor).
  void OnChildFocus(wxChildFocusEvent &event);
  // A theme was applied: the book gets its background and the tabs of the
  // theme.
  void OnThemeApplied(wxCommandEvent &event);
  // The application is closing: each file with changes is closed like the user
  // does (CloseFile() asks Save / Discard / Cancel). Cancel stops the closing.
  // The files are closed one by one, so the ones that were answered before a
  // Cancel stay closed.
  void OnAppClosing(wxCloseEvent &event);

  // ---- Events on the EventNotifier ------------------------------------------
  void SendPageEvent(wxEventType type, const SessionRef &ref,
                     const Account &account = Account::LocalHost(),
                     const SessionRef &oldRef = {},
                     PageActivity activity = PageActivity::None);
  // The side views ask (they arrive on the EventNotifier):
  // The user chose a session in the file explorer or in the activity view.
  void OnShowSessionRequested(PageViewEvent &event);
  // A file or a folder of the file explorer was double clicked.
  void OnFileTreeActivated(FileEvent &event);
  void OnShowDiff(FileEvent &event);
  void OnAppPrefsSaved(wxCommandEvent &event);

  // What the user types in the GotoAnythingBar, and how it closed.
  void OnGotoChanged(wxCommandEvent &event);
  void OnGotoClosed(wxCommandEvent &event);
  // Every page, most recently used first: `first` (if given), then m_recent,
  // then the pages never used, in the order of the tabs.
  std::vector<SessionRef>
  GetPagesByRecency(const std::optional<SessionRef> &first) const;
  void OnPageSwitcherDone(wxCommandEvent &event);
  // The modeless Ctrl+Tab switcher (null when closed) and its rows.
  PageSwitcherDlg *m_switcher{nullptr};
  std::vector<SessionRef> m_switcherPages;
  // Makes `ref` the most recently used page.
  void TouchRecent(const SessionRef &ref);
  // Renames the entries of m_recent that are `from` (a page) to `to`.
  void RenameRecent(const SessionRef &from, const SessionRef &to);
  // Persists the workspace shortly (coalesced); for changes that only touch
  // the order of the recent list.
  void SyncWorkspaceSoon();
  // Rebuilds a full Workspace snapshot from the current UI state (pages +
  // recent list) and writes it as the complete contents of workspace.json. No
  // caller ever incrementally patches the file — every mutation ends here.
  void SyncWorkspaceToDisk();

  // ---- Context menus (MainNotebookMenus.cpp) --------------------------------
  // `on` is the view to pop the menu up on.
  void ShowPageMenu(wxWindow *page, wxWindow *on);
  void ShowSessionMenu(SessionPage *page, wxWindow *on);
  void ShowFileMenu(FilePage *page, wxWindow *on);
  void ShowBackgroundMenu(wxWindow *on);
  // Adds the commands for the group `groupName` (Start Agent, Rename, Close
  // and Refresh Group) to `menu`, under a "Group" sub menu.
  void AppendGroupMenu(wxMenu &menu, const wxString &groupName);

  // ---- Showing pages ----------------------------------------------------
  // Whether `page` is a page of this notebook.
  bool IsPage(wxWindow *page) const;
  // `page` is the tab that is showing: what follows. It does not select the
  // tab (the book did, or ShowPage() did): the file explorer, the title and the
  // status bar, the recent list.
  void PageShown(wxWindow *page, bool updateRecent);
  // Shows some page after the active one was removed; see GetFallbackPage().
  void SelectFallbackPage();

  // ---- Adding / removing pages ------------------------------------------
  // Attaches an already-constructed SessionPage to the book.
  SessionPage *AddSession(SessionPage *page);
  // Creates and adds a terminal/agent page for an existing Session.
  SessionPage *AddSessionPage(const Session &session, bool resume);
  // Adds `page` to the book.
  void AddFilePage(FilePage *page);
  // Removes `page` and destroys it. If it was the active page, another one is
  // shown afterwards.
  void RemovePage(wxWindow *page);

  void DeleteGroupByName(const wxString &name);
  void DeleteAll();
  void RenameGroup(const wxString &oldName);
  void RenameSession(SessionPage *page);
  // Opens the Start Agent dialog pre-filled with `page`'s agent and group,
  // and an auto-generated unique name, so the user launches a fresh,
  // independent session cloned from it.
  void DuplicateSession(SessionPage *page);
  void RefreshGroup(const wxString &groupName);
  void MoveSessionToGroup(const wxString &sessionName,
                          const wxString &fromGroupName,
                          const wxString &toGroupName);

  // Callers reached from a menu/native callback must go through these via
  // CallAfter rather than removing pages directly — deleting a page
  // synchronously from inside such a callback can crash the native macOS
  // outline view mid-redraw.
  // Closes the first session called `sessionName`, whatever its group.
  void CloseSessionByName(const wxString &sessionName);
  void CloseAllFiles();

  void OnSessionIdle(wxCommandEvent &e);
  void OnSessionActive(wxCommandEvent &e);
  void OnSessionExited(wxCommandEvent &e);
  // A page started / stopped working, or is done (OSC 7501).
  void OnSessionProgram(wxCommandEvent &e);
  void OnSessionCwd(wxCommandEvent &e);
  // The spinner turned: the tabs of the working pages get the new frame.
  void OnSpinnerFrame();
  // Shows what `page` is doing (the spinner, the check mark or the hand) as
  // the icon of its tab.
  void ApplyPageIcon(SessionPage *page);
  // Runs the spinner timer while at least one page is working.
  void UpdateSpinnerTimer();
  void OnIdleEvent(wxIdleEvent &e);
  void OnFileSaveStarted(FileEvent &e);
  void OnFileSaveDone(FileEvent &e);
  void OnFileModifiedChanged(FileEvent &e);
  void OnRemoteFileRead(const RemoteReadResult &result);
  // Takes the file out of m_restoringFiles. Whether it was there.
  bool TakeRestoringFile(const wxString &key);
  bool IsRestoringFile(const wxString &key) const;

  void SavePrefs();
  // Gives the toolbars the style of the theme of the application.
  void ApplyToolBarStyles();

  GlyphBook *m_book{nullptr};

  const AdapterRegistry *m_registry{nullptr};
  WorkspaceStore *m_workspaceStore{nullptr};
  AppPaths m_paths;

  // The box on top of the page (Go to line, later quick open). It is the first
  // item of the sizer, and shown only while it is open.
  GotoAnythingBar *m_gotoBar{nullptr};
  // The file the box was opened on, and what the user saw in it: Escape puts it
  // back (only in that file).
  wxString m_gotoFileKey;
  FileViewState m_gotoView;
  // Pages by name, most recently used first: the Ctrl+Tab order, persisted
  // in workspace.json. May name pages that were closed since; the users skip
  // those.
  std::vector<SessionRef> m_recent;
  bool m_syncPending{false};
  // The remote files of the last run that are being fetched again: they stay in
  // workspace.json until the fetch is over, so a quit in between loses none.
  std::vector<SessionFile> m_restoringFiles;
  // Remote files that were being restored when the workspace was cleared: their
  // results are dropped.
  std::set<wxString> m_ignoreRemote;
  // Where to put the caret of the remote files that are being fetched, by key.
  std::map<wxString, FileLocation> m_gotoAfterFetch;
  // The restoring files that the user asked for while they were being fetched:
  // they are selected when they arrive.
  std::set<wxString> m_selectWhenRestored;

  // Per-job run counter (job name -> next sequence number), so consecutive
  // runs of the same job get distinct tab names ("Test Job #1", "#2", ...)
  // instead of colliding with a still-open previous run's tab.
  std::map<wxString, int> m_jobRunCounters;

  // Keys of remote files whose page should close once the save that was
  // started for them succeeds (the user chose "Save" when closing).
  std::set<wxString> m_closeAfterSave;

  // Keys (see FileEvent::MakeKey) of remote files currently being fetched.
  std::set<wxString> m_fetchingRemote;
  // The ones of those that are a reload (see ReloadRemoteFile()).
  std::set<wxString> m_reloadingRemote;

  wxBitmapBundle m_handIcon;
  wxBitmapBundle m_checkIcon;
  Spinner m_spinner{[this](const wxBitmapBundle &) { OnSpinnerFrame(); }};
  int m_pendingIdle{0};
  bool m_idleHandled{false};
};
