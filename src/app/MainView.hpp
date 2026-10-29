#pragma once

#include "UI.hpp"
#include "app/AcceleratorInterceptor.h"
#include "app/FileEvent.hpp"
#include "app/FileViewState.hpp"
#include "app/PageSwitcherDlg.hpp"
#include "app/PageViewEvent.hpp"
#include "app/SessionGroup.h"
#include "app/ThemeManager.h"
#include "core/AppPaths.h"
#include "core/FileLocation.h"
#include "core/Job.h"
#include "core/Workspace.h"

#include <wx/bmpbndl.h>
#include <wx/clntdata.h>
#include <wx/dataview.h>
#include <wx/timer.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class SessionPage;
class FilePage;
class PageRegistry;
class FileExplorerView;
class FilesView;
class GotoAnythingBar;
class FlatView;
class TerminalsView;

class AdapterRegistry;
class WorkspaceStore;
class UiPrefsStore;

static constexpr int kSpinnerFrameCount = 8;

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

// The side panes show the open pages in three lists (FlatView: the sessions,
// FilesView, TerminalsView; each one a projection of the PageRegistry, the
// source of truth for which groups and pages exist). The centre shows the
// selected page. MainView owns the pages (m_sessionsBook) and acts on what the
// user does in the lists. The lists follow the page that is showing through
// wxEVT_PAGEVIEW_SELECTED on EventNotifier.
class MainView : public MainViewBase {
public:
  explicit MainView(wxWindow *parent);
  ~MainView() override;

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

  // Rebuilds UI from the sessions and files persisted in workspace.json.
  // Unsaved edits of a file are not kept. Remote files come back a moment later
  // (they are fetched in the background, so they are not in the order of the
  // recent pages yet: the page used last is only shown if it is a session or a
  // local file).
  void RestoreSessions();

  // The layout of the panes (the AUI perspective) is remembered between runs in
  // the UI prefs. RestoreLayout() applies it (once the window has its final
  // size); SaveLayout() records the current one into the prefs (the caller
  // saves the prefs).
  void RestoreLayout();
  void SaveLayout();

  const std::vector<LoadedTheme> &Themes() const {
    return ThemeManager::Get().Themes();
  }

  void ApplyPrefs();
  void ApplyTheme(const wxString &themeName);
  void ApplyOptimizedDrawing();
  void ApplyFont(const wxFont &f);
  bool CanRefreshCurrent() const;
  void RefreshCurrentSelection();

  // Find in the page that is showing: a file (its bar also replaces) or a
  // session. CanFind() is for the menu items.
  void ShowFind();
  void FindNext(bool forward);
  bool CanFind() const;
  // Whether F3 / Shift+F3 are Find Next / Previous now: in a file, or in a
  // session that has a search. Otherwise a program in the terminal gets F3.
  bool CanFindNext() const;

  // Go to a line of the file that is showing: opens the box on top of the page
  // with ":" in it (GotoAnythingBar); the caret follows what the user types.
  // CanGotoLine() is for the menu item and the key.
  void GotoLine();
  bool CanGotoLine() const;

  // Shows the Ctrl+Tab page switcher (every open session and file, most
  // recently used first) and activates the page chosen when Ctrl is released.
  // If Ctrl is already up, activates the next/previous page directly.
  void SwitchPage(bool forward);

  size_t SessionCount() const;
  // Sessions and files.
  size_t PageCount() const;
  size_t GroupCount() const;

  // Prompts for confirmation, then closes every session in every group.
  void CloseAllSessions();
  // Renames the session that is showing.
  void RenameItem();
  // Whether `groupName` already contains a session named `name`. Names only
  // need to be unique within a group, not across the whole workspace.
  bool IsNameExist(const wxString &name, const wxString &groupName) const;
  // The group of the session that is showing, or nullptr (also for a file).
  SessionGroup *GetSelectedGroup() const;

  bool IsEmpty() const;

  // Logical group names currently in use, excluding the "Terminals" group.
  wxArrayString GetGroupNames() const;

  // The single SessionPage currently shown on the right, or nullptr.
  SessionPage *GetActiveSessionPage() const;
  // The name of the page (session or file) shown on the right, if any.
  std::optional<SessionRef> GetActivePageRef() const;

  // Opens a local file in an editor page under the "Files" container (or
  // reselects it if it is already open).
  // `restoring` (the file was open in the last run) keeps it in the background
  // and only logs a failure.
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

  // Shows `text`, fetched from `path` on `remoteHost`, in an editor page under
  // the "Files" container. Saving writes it back over SFTP. An already-open
  // page is refreshed, unless it has unsaved changes.
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

private:
  void LoadBitmaps();
  // Gives the side panes their default width (kSidePaneWidthDip).
  void ApplyDefaultPaneWidths();

  // ---- The lists of the pages ---------------------------------------------
  void OnPageSelected(PageViewEvent &event);
  void OnPageMenu(PageViewEvent &event);
  // The user chose a session in the choice of the file explorer.
  void OnExplorerSession(PageViewEvent &event);
  // A file or a folder of the file explorer was double clicked.
  void OnFileTreeActivated(FileEvent &event);
  // What the user types in the GotoAnythingBar, and how it closed.
  void OnGotoChanged(wxCommandEvent &event);
  void OnGotoClosed(wxCommandEvent &event);
  // Brings the lists of the pages (the sessions, the files, the terminals) up
  // to date. Coalesced and deferred: it is called from all over, often from
  // inside a view's own event handler.
  void RefreshViews();
  void DoRefreshViews();
  // Every page, most recently used first: `first` (if given), then m_recent,
  // then the pages never used, in group order.
  std::vector<PageInfo>
  GetPagesByRecency(const std::optional<SessionRef> &first) const;
  void OnPageSwitcherDone(wxCommandEvent &event);
  // The modeless Ctrl+Tab switcher (null when closed) and its rows.
  PageSwitcherDlg *m_switcher{nullptr};
  std::vector<PageInfo> m_switcherPages;
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

  // ---- Context menus (MainViewMenus.cpp) --------------------------------
  // `on` is the view to pop the menu up on.
  void ShowPageMenu(wxWindow *page, wxWindow *on);
  void ShowSessionMenu(SessionPage *page, wxWindow *on);
  void ShowFileMenu(FilePage *page, wxWindow *on);
  void ShowBackgroundMenu(wxWindow *on);
  // Adds the commands for the group `groupName` (Start Agent, Rename, Close
  // and Refresh Group) to `menu`, under a "Group" sub menu.
  void AppendGroupMenu(wxMenu &menu, const wxString &groupName);

  // ---- Showing pages ----------------------------------------------------
  // Makes `page` (a SessionPage or a FilePage) the one visible page. It also
  // becomes the most recently used one, unless `updateRecent` is false (what
  // choosing a page in a list does). `notifyViews` sends
  // wxEVT_PAGEVIEW_SELECTED so the views select it; false when a view's own
  // event got us here.
  void ShowPage(wxWindow *page, bool updateRecent = true,
                bool notifyViews = true);
  void SelectSessionPage(SessionPage *page, bool updateRecent = true,
                         bool notifyViews = true);
  void SelectFilePage(FilePage *page, bool updateRecent = true,
                      bool notifyViews = true);
  // The part of showing a page that sessions and files have in common: the
  // book, the views and the recent list.
  void ActivatePage(wxWindow *page, bool updateRecent, bool notifyViews);
  // Shows the group's default page, if any.
  void ActivateGroup(const wxString &groupName);
  // Shows some page after the active one was removed; see
  // PageRegistry::GetFallbackPage().
  void SelectFallbackPage(const wxString &preferredGroup);

  // ---- Adding / removing pages ------------------------------------------
  // Attaches an already-constructed SessionPage to the book and the views.
  SessionPage *AddSession(SessionPage *page);
  // Creates and adds a terminal/agent page for an existing Session.
  SessionPage *AddSessionPage(const Session &session, bool resume);
  // Adds `page` to the book and the views.
  void AddFilePage(FilePage *page);
  // Removes `page` from the views and destroys it. If it was the active page,
  // another one is shown afterwards.
  void RemovePage(wxWindow *page);
  void RemoveEmptyGroups();

  void DeleteGroupByName(const wxString &name);
  void DeleteAll();
  void RenameGroup(SessionGroup *group);
  void RenameSession(SessionPage *page);
  // Opens the Start Agent dialog pre-filled with `page`'s agent and group,
  // and an auto-generated unique name, so the user launches a fresh,
  // independent session cloned from it.
  void DuplicateSession(SessionPage *page);
  void RefreshGroup(SessionGroup *group);
  void MoveSessionToGroup(const wxString &sessionName,
                          const wxString &fromGroupName,
                          const wxString &toGroupName);

  // Callers reached from a menu/native callback must go through these via
  // CallAfter rather than removing pages directly — deleting a page
  // synchronously from inside such a callback can crash the native macOS
  // outline view mid-redraw.
  void CloseSession(const wxString &groupName, const wxString &sessionName);
  // Closes the first session called `sessionName`, whatever its group.
  void CloseSessionByName(const wxString &sessionName);
  // Closes `page` (offering to save unsaved changes) and removes it.
  void CloseFile(FilePage *page);
  void CloseAllFiles();

  void OnSessionIdle(wxCommandEvent &e);
  void OnSessionActive(wxCommandEvent &e);
  void OnSessionExited(wxCommandEvent &e);
  // A page started / stopped working, or is done (OSC 7501).
  void OnSessionProgram(wxCommandEvent &e);
  void OnSessionCwd(wxCommandEvent &e);
  void OnSpinnerTimer(wxTimerEvent &e);
  // Shows what `page` is doing (the spinner, the check mark or the hand) on its
  // row: as the status icon in the list of the sessions, in place of the icon
  // of a terminal.
  void ApplyPageIcon(SessionPage *page);
  // Runs the spinner timer while at least one page is working.
  void UpdateSpinnerTimer();
  void OnIdleEvent(wxIdleEvent &e);
  void OnFileSaveStarted(FileEvent &e);
  void OnFileSaveDone(FileEvent &e);
  void OnRemoteFileRead(const RemoteReadResult &result);
  // Takes the file out of m_restoringFiles. Whether it was there.
  bool TakeRestoringFile(const wxString &key);
  bool IsRestoringFile(const wxString &key) const;

  void SavePrefs();
  // Gives the toolbars the style of the theme of the application.
  void ApplyToolBarStyles();

  const AdapterRegistry *m_registry{nullptr};
  WorkspaceStore *m_workspaceStore{nullptr};
  AppPaths m_paths;

  std::unique_ptr<PageRegistry> m_pages;
  FlatView *m_flatView{nullptr};
  FilesView *m_filesView{nullptr};
  TerminalsView *m_terminalsView{nullptr};
  FileExplorerView *m_fileExplorer{nullptr};
  // The box on top of the page (Go to line, later quick open). It is the first
  // item of the sizer of the centre panel, and shown only while it is open.
  GotoAnythingBar *m_gotoBar{nullptr};
  // The file the box was opened on, and what the user saw in it: Escape puts it
  // back (only in that file).
  wxString m_gotoFileKey;
  FileViewState m_gotoView;
  bool m_viewsRefreshPending{false};
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
  bool m_layoutRestored{false};

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

  std::array<wxBitmapBundle, kSpinnerFrameCount> m_spinnerFrames;
  wxBitmapBundle m_handIcon;
  wxBitmapBundle m_checkIcon;
  wxTimer m_spinnerTimer{this};
  int m_spinnerFrame{0};
  int m_pendingIdle{0};
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor{nullptr};
  bool m_idleHandled{false};
};
