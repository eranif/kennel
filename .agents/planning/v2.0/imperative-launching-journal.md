# Plan: Kennel 2.0.0 TODO list

Progress: `[x]` done, `[ ]` open. Tick a box when its commit is pushed.

## Context
`TODO.md` lists the 12 tasks left before 2.0.0. They range from one-hour changes to whole new
views. This plan orders them so each phase builds on the one before, and each phase is one
reviewable commit (or a few). Decisions taken with the user:
- **Archive** = hide a session but keep it restorable (stored in `workspace.json`).
- **Multi-select** in the flat view: Close, Archive, Move to group, Restart.
- **CI** = new GitHub Actions workflow (there was no `.github/`), modelled on CodeLite's `msys2.yml`.
- **File explorer + git view** = full featured.

Rules to keep (CLAUDE.md): never hand-edit `UI.cpp/UI.hpp/UI.wxcp` (tell the user which widgets to add in
wxCrafter), no exceptions (`Status`/`StatusOr`), `src/core/` GUI-free, wx events instead of
`std::function`, `KLOG_*`, build + clang-format before every commit, commits signed off (`-s`), no
Co-Authored-By, no push without approval.

## Design rule for the new views (user request)
New views are **new, self-contained classes** (`FileExplorer`, `GitView`; the archive dialog too), so they can be
moved to another container (left notebook, a docked pane, a dialog) by changing only the glue code:
- A `wxPanel` subclass with its own `.hpp/.cpp` (+ its own event header). No `#include "app/MainView.hpp"` /
  `MainFrame.h`, no call to `GetMainFrame()`, no listening to `EventNotifier` for page selection.
- **Input** only through setters (e.g. `SetFolder(const WorkFolder &)`); **output** only through wx events sent
  to the parent chain (like `PageViewEvent`), never callbacks / `std::function`.
- Own persistence stays out of the class (the host saves any state through `UiPrefs`).
- `MainView` is the only glue: creates the view, puts it into the notebook (`FillPanel`), calls `SetFolder()`
  when the selected page changes, and handles the view's events (open file, show diff, send `cd`).
- Shared plain data type in `core/WorkFolder.h` (GUI-free): `struct WorkFolder { wxString path; wxString
  wslDistro; bool remote{false}; }`, built by a helper from a `SessionPage` (agent's `IsWSL()` / `IsRemote()`).

## Phase 0 - Drop the tree view (first; decided with the user) - DONE
- [x] New UI-free `src/app/PageRegistry.hpp/.cpp`: same API as `TreeView` for groups/pages (`EnsureGroup`,
  `GetGroup(s)`, `RenameGroup`, `RemoveGroup`, `RemoveEmptyGroups`, `AddSession`, `AddFile`, `RemovePage`,
  `MoveSession`, `SetPageStatusIcon`, `Find*`, `Get*Sessions/FilePages/Pages`, `GetFallbackPage`,
  `GetGroupDefaultPage`, `Clear`) but plain vectors, no widgets, no selection, no events. It also takes the
  constants from `TreeView.hpp` (`kTerminalsGroupName`, `kFilesGroupName`, `IsReservedGroupName`,
  `kGroupIconAliases`). Leaf label is read from the page when `GetPages()` runs (no `UpdateLabel`).
- [x] `MainView`: `m_treeView` -> `m_pages`. "Selected group" now means the group of the session that is
  showing. `ActivateGroup` shows `GetGroupDefaultPage` directly.
- [x] `FlatView`: Group column gets the group's colour icon (`PageInfo::groupIcon`; sort `TextOf` handles icon text).
- [x] Menus (`MainViewMenus.cpp`): `ShowGroupMenu` goes away. An agent-session row menu gets a **Group** submenu
  (Rename Group..., Close Group, Refresh Group, Start Agent in this Group); a file row menu gets Close All Files;
  a terminal row menu gets New Terminal. Empty-space menu stays.
- [x] Remove `TreeView.cpp/.hpp`, the "List/Tree" toolbar tools and `OnShowTreeView`; the left toolbar stays hidden
  until a second left page exists (Phase 6).
- [x] **User task in wxCrafter:** delete the "Tree View" notebook page (`m_panelTreeView`) of `m_notebookLeft` in
  `UI.wxcp` and regenerate.
- [x] Docs: README / BUILDING describe the list.
- [ ] **User task in wxCrafter (left over):** delete the left toolbar (`m_auibar` of `MainViewBase`) if Phase 6
  does not need it, regenerate, then remove `MainView::CreateLeftToolBar()` and the `GetAuibar()` call in
  `ApplyToolBarStyles()` (also in `TODO.md`).
- [ ] The file `CLAUDE.md` line 47 still says `MainView` "owns the session tree" (the user's file; suggested text is in
  the review response of round 4).

## Phase 1 - Flat view (all in `src/app/FlatView.cpp/.hpp`, `PageViewEvent.hpp`)
Order matters: lists first, then filter, then multi-select.

- [x] **Status column + fitted columns** (added on the way, commit `0bcd3f8`): sessions/files list is
  `icon + name | status | icon + group`, terminals list `icon + name | directory`; the status column has a
  fixed width, every other column is fitted to its content after each change of the rows.
- [ ] **Files in their own list + Size column.**
   - Third list `m_fileList` (Name | Folder | Size | hidden key) next to `m_list` (sessions) and
     `m_terminalList`. Routing like terminals: add `IsFilesGroup()` (`kFilesGroupName`, `PageRegistry.hpp`) and
     extend `ListFor()` / `RefOf()` / `FindRow()` / `SelectPage()`.
   - Layout: nested `wxSplitterWindow` (top: sessions; bottom: a second splitter with files | terminals).
     New pref `flatViewSashPosition2` in `UiPrefs.h` + `UiPrefsStore.cpp` (same pattern as
     `flatViewSashPosition`), restored/saved in `MainView::RestoreLayout/SaveLayout`.
   - `PageInfo` (`PageViewEvent.hpp`) gets `wxString folder` and `int64 size` (-1 = none), filled by
     `PageRegistry::GetPages` from a new `FilePage::GetSize()` (editor length / disk size) and
     `FilePage::GetPath()`. Size column sortable via `FlatViewStore::Compare` (numeric, not text).
     Refresh the size after a save: send the existing file-saved event -> `RefreshFlatView()`.
- [ ] **Filter box** on top of the flat view: `wxSearchCtrl` above the splitter. Keep `m_allPages`
  (the last `SetPages` input); the filter rebuilds the rows from it (case-insensitive substring on
  label/group/folder). `SetPageStatusIcon` also updates `m_allPages`. Esc clears. Selected page stays selected if
  it still matches. Filter text is not persisted.
- [ ] **Multi-select.** Switch the lists from `wxDV_SINGLE` to `wxDV_MULTIPLE`.
   - A single selected row behaves as today (shows the page). With 2+ rows selected, no page switch.
   - Selection stays inside one list: starting a selection in a list clears the others.
   - `PageViewEvent` gets `std::vector<SessionRef> m_refs` (+ `GetRefs()/SetRefs()`); `OnContextMenu`
     fills it with all selected rows.
   - `MainView` context menu (`MainViewMenus.cpp`): when `GetRefs().size() > 1` show **Close**, **Archive**,
     **Move to Group >**, **Restart**; each loops over the refs (resolve refs to pages first, because closing
     changes the registry). Reuse the existing single-page handlers (close, restart, move-to-group in `MainView`).

## Phase 2 - Files saved as part of the session
- [ ] `core/Workspace.h`: `struct SessionFile { wxString path; wxString remoteHost; wxString remoteUser; }`
  and `Workspace::files`.
- [ ] `core/WorkspaceStore.cpp`: write/read a `"files"` array (same style as `groups`); old files without
  it still parse.
- [ ] `MainView::SyncWorkspaceToDisk()`: add `m_pages->GetFilePages()`, and put files in `recentSessions` too
  (their `SessionRef` group is `kFilesGroupName`).
- [ ] Call `SyncWorkspaceSoon()` after `AddFilePage()` and in `CloseFile()`.
- [ ] `MainView::RestoreSessions()`: reopen local files with `OpenLocalFile()` (skip a missing file with
  `KLOG_WARN`); remote files with the existing `OpenRemoteFile()` path (async; a failure only logs).
  Restore them before the "last used page" is selected. Unsaved edits are not persisted (note in the doc comment).

## Phase 3 - Go to line (file editor)
- [ ] Mirror how Find is wired: menu item in `MainFrame.cpp` (`searchMenu`, "Go to Line...\tCtrl+G", bind +
  `UpdateUI` enabled only for a `FilePage`), a key in `AcceleratorInterceptor.cpp` (like `wxID_FIND`),
  `MainView::GotoLine()` -> `FilePage::GotoLine()`. Dialog: a small `wxGetNumberFromUser`-style prompt
  "Line (1 - N)"; then `GotoLine(n-1)`, centre the caret line (`wxStyledTextCtrl`), focus the editor. Add
  `Editor::GotoLine` wrapper in `Editor.h/.cpp`.

## Phase 4 - Settings: review timeout and turns
- [ ] `UiPrefs.h` + `UiPrefsStore.cpp`: `reviewMaxRounds{5}` (clamp 1-50) and `reviewStallMinutes{20}`
  (clamp 1-240).
- [ ] **User task in wxCrafter** (I must not edit `UI.*`): in `SettingsDlg` add two `wxSpinCtrl`:
  `m_spinCtrlReviewTurns` (min 1, max 50) and `m_spinCtrlReviewTimeout` (min 1, max 240), with labels
  "Review turns" / "Review timeout (minutes)", then regenerate. I will wait for that before touching
  `SettingsDlg`.
- [ ] `SettingsDlg.cpp`: load the values in the constructor, add getters in `SettingsDlg.hpp`;
  `MainFrame::OnSettings` copies them into prefs (next to `scrollbackLines`).
- [ ] `ReviewBuddy.cpp`: `ReviewLoop(ReviewLoop::NewId(), prefs.reviewMaxRounds)` (line ~177) and replace the
  constant `kStallTimeout` with `std::chrono::minutes(prefs.reviewStallMinutes)` read at the check (line
  ~217). `ReviewLoop` stays GUI-free (it already takes `maxRounds`).

## Phase 5 - Archive
- [ ] `core/Workspace.h`: `struct ArchivedSession { Session session; wxString archivedAt; }` and
  `Workspace::archived`; `WorkspaceStore` reads/writes `"archived"`.
- [ ] `MainView::ArchiveSessions(refs)`: only agent sessions (not terminals/jobs/files; the menu item is
  disabled for those). Copy the `Session` into `m_archived`, close the page without the "exit" prompts (reuse
  the close path used by the context menu), `SyncWorkspaceToDisk()` writes `m_archived`.
- [ ] `MainView::RestoreArchived(index)`: `LaunchSession` with `resume = true`; if the name exists in the group,
  add a numeric suffix (same approach as the terminal naming in `StartTerminal`).
- [ ] Entry points: context menu **Archive** (single and multi-select, Phase 1) and a menu item
  **Archived Sessions...** in `MainFrame.cpp`.
- [ ] New hand-written dialog `ArchiveDlg` (`wxDataViewListCtrl`: Name | Agent | Folder | Archived at;
  buttons Restore, Delete, Close). It is not wxCrafter-generated; the user can move it there later.

## Phase 6 - File explorer
- [ ] New self-contained class **`FileExplorer`** (`src/app/FileExplorer.hpp/.cpp`, events in
  `src/app/FileExplorerEvent.hpp`; `wxDataViewTreeCtrl`, lazy children). API: `SetFolder(const WorkFolder &)`
  (root; empty clears the tree), `Refresh()`. It knows nothing about sessions, pages or `MainView`.
- [ ] Events (to the parent chain, `FileExplorerEvent` with `GetPath()`): `wxEVT_FILE_EXPLORER_OPEN` (double-click a
  file), `wxEVT_FILE_EXPLORER_CD` ("Open terminal here" / cd). File actions (New File, New Folder, Rename,
  Delete with confirm, Copy Path, Reveal in file manager) are done inside the class with
  `wxFileName`/`wxRenameFile`/`wxRemove`; errors via `Status`, shown in the panel.
- [ ] Directory listing reuses `FileProvider` (`FileBrowserDlg.hpp`: `LocalFilesProvider`, `WSLFilesProvider`) picked
  from `WorkFolder::wslDistro`. Remote (SSH) folder: the panel shows "Not available for remote sessions" (no
  SFTP provider exists yet).
- [ ] Glue in `MainView` only: create it in the constructor, add it as a page of `GetNotebookLeft()` with
  `FillPanel(...)`, add a toolbar tool in `CreateLeftToolBar()` (new `assets/explorer.svg`; CMake globs
  `assets/*.svg`), call `SetFolder()` when the selected page changes (`SelectSessionPage`), and bind the two
  events: OPEN -> `OpenLocalFile`, CD -> send `cd <path>` to the active terminal. `prefs.leftTab` already
  stores the page index.
- [ ] Refresh: on expand, after each action, and `Refresh()` when the app gets focus (re-list expanded nodes only).

## Phase 7 - Git view
- [ ] GUI-free `src/core/GitRunner.h/.cpp`: runs `git` in a folder (worker thread, `AliveFlag` pattern from
  `SessionPage`; results posted as wx events) and parses `git status --porcelain=v2 -b`,
  `git branch`, `git log --oneline`, `git diff [--cached] -- file`. Returns `StatusOr<T>`. WSL sessions:
  run through `wsl.exe -d <distro> --cd <dir> git ...`. Remote sessions: disabled in v1.
- [ ] New self-contained class **`GitView`** (`src/app/GitView.hpp/.cpp`, events in `src/app/GitViewEvent.hpp`;
  it owns a `GitRunner`). API: `SetFolder(const WorkFolder &)` (finds the repo; empty state when the folder is
  not in a git repo or is remote), `Refresh()`. It knows nothing about sessions, pages or `MainView`.
- [ ] UI inside the class: branch choice (checkout), lists of Staged / Unstaged / Untracked (context menu
  Stage, Unstage, Discard with confirm), commit message box + **Commit** (and amend), **Pull** / **Push**
  buttons, recent log.
- [ ] Diff: double-click a change sends `wxEVT_GIT_VIEW_SHOW_DIFF` (`GitViewEvent`: `GetPath()`, `GetTitle()`,
  `GetDiffText()`); the host decides where to show it. `MainView` glue opens a read-only page with the text in
  `Editor` using the diff lexer (`wxSTC_LEX_DIFF`; add `Editor::SetDiffLexer` next to the other lexers in
  `Editor.cpp`), as a `FilePage`-like page in the Files group (key `git-diff:<path>`). Also
  `wxEVT_GIT_VIEW_OPEN_FILE` for "Open file" in the context menu.
- [ ] Glue in `MainView` only (same as the explorer): page of `GetNotebookLeft()` + toolbar tool (`assets/git.svg`),
  `SetFolder()` on page selection, bind the events.
- [ ] Refresh: after every action, `Refresh()` on app focus, and whenever the host calls `SetFolder()`.

## Phase 8 - Windows installer CI - DONE (commit `342f59c`, pushed)
- [x] `.github/workflows/msys2.yml` modelled on `C:\msys64\home\eran\devl\codelite\.github\workflows\msys2.yml`:
  triggers `workflow_dispatch`, `push` to `main`, `pull_request` (ignore `**.md` and `.agents/**`), plus tags
  `v*`; `concurrency` cancels a run that a newer push replaces; one job on `windows-latest` (MSYS2 `clang64`).
- [x] `msys2/setup-msys2@v2` with the packages of `BUILDING.md` plus `glew` and `llvm-openmp` (DLLs the installer
  copies); `actions/checkout`.
- [x] Cache `.build-release/wxWidgets-install` (the slow step), key from `hashFiles('build.sh')`. (The static
  OpenSSL of `local_builds` is macOS only; Windows uses the MSYS2 package.)
- [x] Inno Setup check (installs it with choco if the image has none); `./build.sh package` builds Kennel and
  runs `make setup/fast`. Checked locally: it creates `kennel-<version>-amd64-installer.exe`.
- [x] Upload `.build-release/kennel-*-installer.exe` as an artifact; silent-install smoke test
  (`/SILENT /SP- /SUPPRESSMSGBOXES /NOICONS /NORESTART`) that fails if `kennel.exe` is not in
  `%LOCALAPPDATA%\Programs\Kennel`.
- [x] Windows on ARM (`clangarm64`, `windows-11-arm`) is switched off for now (comment in the matrix).
- [x] `BUILDING.md` and `TODO.md` describe the CI.
- [ ] Check the first run on GitHub (Actions tab) and fix what fails; then tick the item in `TODO.md`.
- [ ] Attach the installer to the GitHub release on `v*` tags (`softprops/action-gh-release`), after the first
  green run.
- [ ] Add Windows on ARM back: CMake hardcodes `libcrypto-3-x64.dll`, and `installer.iss.in` allows x64 only.

## Suggested order and commits
0 Drop the tree view (done) -> 8 CI (done) -> 1 Flat view (files list + size, filter, multi-select; the status
column is done) -> 2 Files in session -> 3 Go to line -> 4 Settings (after the wxCrafter change) -> 5 Archive
(needs Phase 1 multi-select for the bulk item) -> 6 File explorer -> 7 Git view.

## Verification (every phase)
- `export MSYSTEM=CLANG64 PATH=/c/msys64/clang64/bin:$PATH; ./build.sh` ("Kennel built successfully"; close
  any running Kennel first, or the installer copy step fails), then clang-format (CLAUDE.md command).
- Manual checks in the running app (no test suite):
  - P1: open sessions, terminals, 3 files -> three lists with the sizes; filter hides non-matches and keeps the
    selection; Ctrl/Shift-select 3 sessions -> context menu bulk items work; sash positions survive a restart.
  - P2: open local + remote files, restart -> same files back, same last page; missing file is skipped.
  - P3: Ctrl+G in a file page jumps and centres the line; disabled on a terminal.
  - P4: change turns/timeout, start a review buddy -> round text `round 1/N`; stall after the timeout.
  - P5: archive a session (terminal closes), restart Kennel (still archived), restore (agent resumes).
  - P6: explorer follows the selected session; every context action; WSL session lists the distro files.
    Check that `FileExplorer.cpp/.hpp` has no include of `MainView`/`MainFrame` (grep) - the rule above.
  - P7: in a repo with changes: stage/unstage/commit/branch switch/diff; a non-git folder shows an empty state.
    Same grep for `GitView`; as a test of the rule, `GitView` should work when put in a plain `wxFrame`.
  - P8: push the workflow to a branch, check the Actions run and download the installer artifact.
- Old `workspace.json` / `.persist.json` files (without the new keys) must still load (open one before the change).
