#pragma once

#include "UI.hpp"
#include "app/AcceleratorInterceptor.h"
#include "app/FileEvent.hpp"
#include "app/PageViewEvent.hpp"
#include "app/SessionGroup.h"
#include "app/ThemeManager.h"
#include "core/AppPaths.h"
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
class TreeView;
class FlatView;

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

// The left side shows the open pages twice: as a Groups -> Sessions tree
// (TreeView, the source of truth for what exists) and as a flat list
// (FlatView, a projection of the tree). The right side shows the selected
// page. MainView owns the pages (m_sessionsBook) and acts on what the user
// does in the views. The views keep their selections in sync themselves,
// through wxEVT_PAGEVIEW_SELECTED on EventNotifier.
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

  // Rebuilds UI from sessions persisted in workspace.json.
  void RestoreSessions();

  // The two splitters (left pane | terminal, and session tree / flat list)
  // are remembered between runs in the UI prefs. RestoreLayout() applies the
  // saved positions once the window has its final size; SaveLayout() records
  // the current ones into the prefs (the caller saves the prefs).
  void RestoreLayout();
  void SaveLayout();

  const std::vector<LoadedTheme> &Themes() const {
    return ThemeManager::Get().Themes();
  }

  void ApplyPrefs();
  void ApplyTheme(const wxString &themeName);
  void ApplyOptimizedDrawing();
  void ApplyFont(const wxFont &f);
  void RefreshSelectedGroup();
  bool CanRefreshCurrent() const;
  void RefreshCurrentSelection();

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
  bool IsSelectionSessionGroup() const;
  bool IsSelectionTerminalGroup() const;
  void RenameItem();
  // Whether `groupName` already contains a session named `name`. Names only
  // need to be unique within a group, not across the whole workspace.
  bool IsNameExist(const wxString &name, const wxString &groupName) const;
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
  void OpenLocalFile(const wxString &path);

  // Reads `clickedPath` (resolved against `searchDirs`) from `remoteHost` on
  // a worker thread, then shows it via ShowRemoteFile(). Ignored while the same
  // file is already being fetched.
  void OpenRemoteFile(const RemoteHostDetails &remoteHost,
                      const wxString &clickedPath,
                      const std::vector<wxString> &searchDirs);

  // Shows `text`, fetched from `path` on `remoteHost`, in an editor page under
  // the "Files" container. Saving writes it back over SFTP. An already-open
  // page is refreshed, unless it has unsaved changes.
  void ShowRemoteFile(const RemoteHostDetails &remoteHost, const wxString &path,
                      const wxString &text, bool editable);

private:
  void LoadBitmaps();

  // ---- The two views ----------------------------------------------------
  void OnPageSelected(PageViewEvent &event);
  void OnPageMenu(PageViewEvent &event);
  // Brings the flat list up to date. Coalesced and deferred: it is called
  // from all over, often from inside a view's own event handler.
  void RefreshFlatView();
  void DoRefreshFlatView();
  // Every page, most recently used first: `first` (if given), then m_recent,
  // then the pages never used, in tree order.
  std::vector<PageInfo>
  GetPagesByRecency(const std::optional<SessionRef> &first) const;
  // Makes `ref` the most recently used page.
  void TouchRecent(const SessionRef &ref);
  // Renames the entries of m_recent that are `from` (a page) to `to`.
  void RenameRecent(const SessionRef &from, const SessionRef &to);
  // Persists the workspace shortly (coalesced); for changes that only touch
  // the order of the recent list.
  void SyncWorkspaceSoon();
  // Rebuilds a full Workspace snapshot from the current UI state (tree +
  // recent list) and writes it as the complete contents of workspace.json. No
  // caller ever incrementally patches the file — every mutation ends here.
  void SyncWorkspaceToDisk();

  // ---- Context menus (MainViewMenus.cpp) --------------------------------
  // `on` is the view to pop the menu up on.
  void ShowGroupMenu(const wxString &groupName, wxWindow *on);
  void ShowPageMenu(wxWindow *page, wxWindow *on);
  void ShowSessionMenu(SessionPage *page, wxWindow *on);
  void ShowFileMenu(FilePage *page, wxWindow *on);
  void ShowBackgroundMenu(wxWindow *on);

  // ---- Showing pages ----------------------------------------------------
  // Makes `page` (a SessionPage or a FilePage) the one visible page. It also
  // becomes the most recently used one, unless `updateRecent` is false (what
  // choosing a page in the flat list does). `notifyViews` sends
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
  // Selects the group node and shows the group's default page, if any.
  void ActivateGroup(const wxString &groupName);
  // Shows some page after the active one was removed; see
  // TreeView::GetFallbackPage().
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
  // CallAfter rather than removing pages directly — deleting a tree leaf
  // synchronously from inside such a callback can crash the native macOS
  // outline view mid-redraw.
  void CloseSession(const wxString &groupName, const wxString &sessionName);
  // Closes the first session called `sessionName`, whatever its group.
  void CloseSessionByName(const wxString &sessionName);
  // Closes `page` (offering to save unsaved changes) and removes its leaf.
  void CloseFile(FilePage *page);
  void CloseFileByKey(const wxString &key);
  void CloseAllFiles();

  void OnSessionIdle(wxCommandEvent &e);
  void OnSessionActive(wxCommandEvent &e);
  void OnSessionExited(wxCommandEvent &e);
  void OnIdleEvent(wxIdleEvent &e);
  void OnFileSaveStarted(FileEvent &e);
  void OnFileSaveDone(FileEvent &e);
  void OnRemoteFileRead(const RemoteReadResult &result);

  void SavePrefs();

  const AdapterRegistry *m_registry{nullptr};
  WorkspaceStore *m_workspaceStore{nullptr};
  AppPaths m_paths;

  TreeView *m_treeView{nullptr};
  FlatView *m_flatView{nullptr};
  bool m_flatRefreshPending{false};
  // Pages by name, most recently used first: the Ctrl+Tab order, persisted
  // in workspace.json. May name pages that were closed since; the users skip
  // those.
  std::vector<SessionRef> m_recent;
  bool m_syncPending{false};
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

  std::array<wxBitmapBundle, kSpinnerFrameCount> m_spinnerFrames;
  int m_pendingIdle{0};
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor{nullptr};
  bool m_idleHandled{false};
};
