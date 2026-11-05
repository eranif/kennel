# Kennel 2.0.0 Release Notes

*Covers changes since tag `1.4.0`.*

Kennel 2.0.0 turns the terminal page into a full-featured workspace. Each session now has a
toolbar (Restart, Review Buddy, Terminal, Copy, Paste, Find) in place of the right-click menu,
along with a header that identifies the session, a search bar and a thin progress bar. Review
Buddy reports its progress directly in the header.

The main window has been redesigned around a single notebook for all sessions and files, a
**Side Bar** that hosts a **File Explorer** and a **Git** view, and an **Activity** pane that
shows which sessions are working, done or blocked.

Highlights:

- Sessions can be **archived** and restored later.
- **Goto Anything** (Ctrl+Shift+P) provides quick access to any command, page or action.
- On first start, Kennel detects the installed agents and defines them automatically.
- The whole application can follow the terminal theme.
- File links in the terminal respect `cd` and open at the referenced line.
- Open files are restored together with the session.
- On Windows, Kennel ships a newer ConPTY and wxWidgets, and the installer is now built on
  GitHub Actions.

## Major Improvements

### A new main window

- **A single notebook for sessions and files.** The session tree, the flat list and the separate
  lists of files and terminals have been removed. Every session, terminal and file is now a tab
  of one notebook.
  - Tabs use the colors of the terminal theme and update when the theme changes.
  - A middle click closes a tab. A close button on each tab is available as an option.
  - The tooltip of a tab displays its full title.
  - **Ctrl+Tab** continues to switch between pages, in most-recently-used order.
  - **Closing a tab activates the previously used page.** The page selected is the next one in
    the recent order, as Ctrl+Tab would select, rather than the adjacent tab. Closing a group
    behaves the same way.
  - **Program status on tabs.** A program that reports its state through OSC 7501 is indicated
    on its tab by a spinner (working), a green check mark (done) or a hand (blocked, awaiting
    input). The indicator is cleared once the page is viewed. The reviewer's terminal is
    tracked as well.
- **Side Bar.** A new pane with two tabs: the **File Explorer** and the **Git** view (see
  "Git in the Side Bar"). The selected tab is restored on the next start, and **Settings**
  allows the tab bar to be placed at the top or at the bottom.
  - The Side Bar maintains one tree and one git view per location. A location is the folder of
    a session on this machine, in a WSL distribution or on a remote host. Both views follow the
    active page.
  - Each **terminal** has its own tree and git view, which follow the current folder of the
    shell.
  - Double-click a file to open it. Typing in the tree selects the next entry that begins with
    the typed characters.
  - A **WSL** session lists the files of its distribution, and a session on a **remote host**
    lists the files of that host. A remote tree connects in the background and displays
    `Connecting...` until it is ready, so a slow host does not block the window.
  - The tree shows hidden files. Its context menu provides **Refresh**, **New Folder...**,
    **New File...**, **Delete...**, **Show in Finder / Explorer / File Manager**,
    **Open with Default App** and **Copy Path**. Delete is never the first entry and always
    asks for confirmation.
  - Multiple files can be selected to open or delete them together.
- **Activity.** A new pane that lists the sessions whose program is working, done or blocked,
  each with an icon (a spinner, a check mark or a hand).
  - Blocked sessions are listed first, followed by done and then working sessions.
  - A working or blocked row remains for as long as the program stays in that state, including
    for the page currently in view.
  - A done row is removed when the session is activated.
  - Clicking a row shows the corresponding session.
- **Tab groups.** The tabs of the sessions in a group are displayed together under a header
  that carries the name of the group. Files and Terminals have their own groups. **Move To
  Group** and **Rename Group** update the tabs immediately, and **Group Tabs by Session Group**
  in the context menu of the tab bar (right-click an empty area) disables the grouping. The tab
  order is saved and restored on the next start.
- **Tab options.** **Settings** now provides:
  - the **tab position** (top, bottom, left or right);
  - the **tab outline color**;
  - a **Close button on the tab**.

  Tabs follow a browser-style design. A file tab displays the folder and the file name
  (`c/file.cpp`), with the full path in its tooltip, and a file with unsaved changes is marked
  with an orange dot on its icon. The tab position can also be changed from the context menu of
  the tab bar (right-click on an empty area and choose **Tab Position**). Tabs are placed at the
  top by default.
- **View menu.** **Side Bar** and **Activity** show or hide the corresponding pane.
  **Toggle All Views** (**Ctrl+M**) hides every pane except the pages, or restores them. The
  shortcut also works inside a terminal, and it uses the Ctrl key on macOS as well.
- **View → Reset Kennel Layout.** Restores the pane layout of the first start. The layout is
  saved between runs, and a pane that is unknown to a saved layout is shown again. Side panes
  start at a width of 300 DIP and float at 400x600 DIP.
- **Terminal theme integration.** The application can follow the terminal theme. See "A theme
  for the whole application".
- **Faster remote files.** Kennel keeps its SFTP connections open (one pool per host) instead of
  establishing a new SSH connection for each call. Opening a remote file no longer waits for a
  new handshake, which could take several seconds on a distant host.

### Git in the Side Bar

- **Git view.** The second tab of the Side Bar shows the git status of the current location: a
  local folder, a WSL distribution or a remote host (over SSH). It displays the branch and a
  list of changed files with their status and a file type icon.
  - The view refreshes when a file is saved and after each action.
  - Double-clicking a file shows its **diff** in a read-only page that is not saved with the
    session. The editor highlights added and deleted lines.
  - The context menu provides **Show Diff** and **Open File**.
- **Actions.** The toolbar provides one button per action. Actions apply to multiple selected
  files.
  - **Add** (the **+** button) stages the selected files.
  - **Reset** restores the selected files. **Reset Repo** resets the entire repository after
    confirmation.
  - **Commit** opens a message dialog with the options **Amend** and **Signoff** (the Signoff
    choice is remembered). The commit runs in the background, and any errors are reported.
  - **Log** prompts for the number of entries (100 by default) and displays the log in an
    editor.
  - **Blame** shows the blame of the selected file.
  - **Push** and **Pull** run in a terminal named `Git (<place>)`, which Kennel opens or reuses
    on the same host. Git can therefore prompt for credentials where it normally would.
- **Alternative root folder.** The explorer and the git view can show a folder other than the
  session folder. It can be selected in two ways:
  - Right-click a folder in the file explorer and choose **Set This Folder as Root**.
  - Click **Folder** on the git view toolbar and browse to a folder, including a parent of the
    current one. The browser works the same way for the local machine, WSL and remote hosts.

  Only the active session changes folder; other sessions at the same location keep theirs. The
  selection is saved with the session.

### Archive a session

- **Archive.** A session can be archived instead of closed. Its tab is removed, and Kennel
  stores the session together with the archive date in `workspace.json`. Use **Archive** in the
  tab menu. If the agent is busy, Kennel asks for confirmation first. Terminals cannot be
  archived.
- **File → Archived Sessions...** lists the archived sessions (name, agent, folder and date) and
  provides a filter box. The list can be sorted by any column, with the newest entry first.
  - **Restore** restarts the agent using its resume option. If the original name is already in
    use, a number is appended. The dialog remains open so that several sessions can be restored
    in a row.
  - **Delete** permanently removes the entry.
- **Close button behavior.** When an agent session is closed (with the close button of the tab
  or with **Close** in its menu), Kennel asks whether to **Archive**, **Close** or **Cancel**.
  The **Do not ask again** option stores the answer. The behavior can be changed later in
  **View → Close Button of a Session** (Ask, Archive or Close).

### Goto Anything

- **Ctrl+Shift+P** opens a dialog with a search field and a result list, and no buttons. It is
  also available from **Search → Goto Anything...** and works inside a terminal. Typing filters
  the list by words. Use the arrow keys or Page Up / Page Down to navigate, Enter to select and
  Escape to close.
- **Searchable items:**
  - actions of the current page: **Restart Session** or **Restart Terminal**,
    **Start Review Buddy with ...** for each agent that can review the session, and
    **Close Review Buddy**;
  - open pages: `Select Session: ...`, `Select Terminal: ...` and `Select File: ...`;
  - **every enabled item of the menu bar**, displayed with its keyboard shortcut. The **Edit**
    menu is included only while a file is in view.

### The first start

- **Automatic agent detection.** On the first start, Kennel searches this machine in the
  background for `claude`, `codex` and `kiro-cli` and defines an agent, with the appropriate
  icon, for each tool it finds. The scan runs once (`localAgentsScanned` in `.persist.json`)
  and shows no dialog. Tools that already have an agent are not added again, and upgrading from
  an earlier version does not trigger the scan.
- **WSL and SSH hosts.** Kennel then asks once, in a single dialog, whether to search your WSL
  distributions (Windows only) and the hosts defined in your SSH configuration. Distributions
  are preselected, and hosts are selected manually. This search also runs in the background:
  - progress is shown in the status bar;
  - ssh never prompts for a password (`BatchMode`);
  - a summary lists the agents that were added.

  Agents are named, for example, `Claude (WSL: Ubuntu)` or `Codex (myhost)`, and use the WSL or
  remote icon.
- **Larger initial window.** For a new user, the window opens centered and covers 80% of the
  screen.

### The editor and the files

- **Edit menu.** The menu is shown in the menu bar only while a file is in view, so that a
  terminal retains its key bindings (Ctrl+R, Ctrl+C, Ctrl+D and others). It provides
  **Close Editor** (Ctrl+W), **Reload File**, **Undo**, **Redo**, **Copy**, **Paste**,
  **Select All**, **Delete Line** and **Duplicate Line**, together with the options
  **Wrap Lines**, **Highlight Braces** and **Highlight Indent Lines**. The options are saved,
  and open editors apply changes immediately. Wrap Lines is also available on the file toolbar.
- **New File and Open File.** **File → New File** (Ctrl+N) opens an empty `Untitled` editor, and
  Save prompts for a file name. **File → Open File** (Ctrl+Shift+O) opens an existing file.
  **Rename Session** (F2) has moved to the File menu.
- **Protection of unsaved changes.** When Kennel closes, each file with unsaved changes prompts
  to **Save**, **Discard** or **Cancel**. Cancel keeps the application open.
- **Diff and Rust.** `.diff` and `.patch` files are syntax-highlighted, and Rust is now a
  supported language.
- **Centered caret line.** The caret line is centered when a search match or Go to Line moves
  the caret.

### A toolbar and a header for every terminal

- **Terminal toolbar.** A toolbar above every session provides **Restart**, **Review Buddy**,
  **Terminal**, **Copy**, **Paste** and **Find**. The terminal no longer has a right-click menu,
  because its actions are now in the toolbar. The **Clear buffer** entry of the old menu has
  been removed.
  - **Copy** copies the selected text. When nothing is selected, it copies the text that the
    program in the terminal placed on the clipboard itself (for example tmux, or a TUI such as
    Claude Code), in full rather than only its first line. It is disabled when there is nothing
    to copy.
  - **Paste** and **Find** act on the terminal that has the focus (the agent terminal or the
    reviewer terminal).
  - **Terminal** opens a terminal named `<Session> | Terminal` on the same host and in the same
    folder as the session (local, WSL or remote). Clicking it again returns to that terminal.
    It is not shown for plain terminals.
  - **Review Buddy** is a drop-down menu.
    - When no review is open, it lists the agents on the same host that can act as reviewers
      ("Review with ..."). If a review is not possible, it explains why (for example, no
      working directory, or not a git repository).
    - When a review is open, it shows the current state and offers **Send the Request Again**,
      **Stop the Review**, **Open the Latest Review** and **Close Review Buddy**.
    - It is disabled for plain terminals and scheduled jobs.
- **Terminal header.** A strip above the agent terminal shows
  `Agent Name | Title | Working Directory`. It follows the terminal title and the current folder
  of the shell, and it shows the start folder of the session until the shell reports one. A
  plain terminal shows `Terminal`. On dark themes, the text is yellow on a strip that is darker
  than the terminal.
- **Review Buddy status in the header.** While a review is open, the header shows only the state
  of the review. A braille spinner (⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧) is animated while work is in progress, for
  example `⠹ Waiting for review (round 1/5)` or `⠼ Addressing comments (round 2/5)`. When the
  review ends, a marker precedes the status:
  - 👍 the review is clean;
  - 👎 the review stalled or failed;
  - 🔴 the round limit was reached with findings still open.

  The header returns to `Agent | Title | Folder` when Review Buddy is closed.
- **Progress bar.** A thin bar above the terminal displays progress reported by a program through
  the OSC 9;4 sequence (the ConEmu / Windows Terminal progress bar) or through OSC 7501 (the
  program status protocol). It shows either a percentage or an indeterminate animation, is
  hidden when there is no progress, and uses only a few pixels of the terminal area. To test it,
  run `printf '\e]9;4;1;40\a'`, and use `\e]9;4;0;0\a` to hide it.
- **Toolbar icons for every theme.** New blue icons are provided for Copy, Paste, Find, Save,
  Close and Reload. Disabled toolbar buttons now match the theme; previously the disabled image
  appeared glossy and white on dark themes.

### A theme for the whole application

- **Follow terminal theme, System, Dark or Light.** A new **Theme** option in **Settings**
  controls the appearance of the application. It is stored in `.persist.json` (`appTheme`) and
  applied at startup and when the settings are confirmed. The default, **Follow terminal
  theme**, makes the application dark or light according to the background of the terminal
  theme, including while a terminal theme is being selected.
- **Theme-aware toolbars.** Light themes use a plain toolbar background, and dark themes use the
  default one. Changing the settings updates the toolbars of all open sessions and files
  immediately.
- **Dark and light icon sets.** The icons of toolbars, menus and lists (agent, terminal, job,
  code reviewer, reload, save, close, find, copy, paste, up and home) use a separate shade of
  gray for each theme and follow the application theme.
- **Row stripes on light themes.** The alternating row color of lists was derived from a
  lightened window color, which has no visible effect on white. Light backgrounds now use darker
  rows so that the stripes are visible.
- **Editor.** Line numbers and comments are legible on any theme, using a dimmed text color and a
  muted green with a minimum contrast. The caret line is highlighted, and the caret is wider.
- **Terminal themes on light backgrounds.** Many theme files do not define a cursor color, and
  the default is white, which made the block cursor invisible on light themes. The following
  changes address this:
  - a theme without a cursor color uses its foreground color;
  - the selection colors (`[colors.selection]`) and search match colors
    (`[colors.search.matches]`) of a theme are now honored, and when a theme defines only one
    color of a pair (for example `CellForeground`), a readable counterpart is chosen
    automatically;
  - the link color is the blue defined by the theme (the bright variant on dark backgrounds);
  - a color value followed by a comment (`'#303446' # base`) is no longer ignored.

### Search in files and in terminals

- **Search bar replaces the dialog.** Ctrl+F (Cmd+F on macOS), **Find** in the Search menu, or
  **Find** on the terminal toolbar opens a search bar at the bottom of the page, below the editor
  or the terminals. Searching is incremental.
  - Enter or the down arrow finds the next match.
  - Shift+Enter or the up arrow finds the previous match.
  - Escape closes the bar.
  - The selected text is used as the initial search term.
- **Files.** The bar also provides **Full word** and **Case sensitive**, and a second row with
  **Replace with**, **Replace** and **Replace all** (a single undo step). The Find and Replace
  dialog and the Replace menu entry have been removed. Tab moves between the find and replace
  fields.
- **Terminals.** The bar provides the search field and the navigation arrows, and closing it
  clears the highlights. This also fixes Find on macOS, where Enter had no effect in the old
  dialog.
- **F3 and Shift+F3.** Find the next and the previous match, regardless of the search direction.
  A terminal keeps F3 for the program running in it (such as `mc` or `htop`) until a search has
  been started. On Windows and Linux, Ctrl+F is no longer passed to the program in the
  terminal, so the readline command "forward-char" is not available there.

### Files opened from the terminal

- **Relative paths follow `cd`.** Kennel queries the shell for its current folder (using OSC 7,
  and OSC 9;9 for cmd.exe). A clicked relative path is resolved against that folder first, then
  against the folder where the session started, and against the home folder only if no other
  folder is known. This works in plain terminals, in WSL and over SSH. The terminal injects the
  shell integration for bash (including over `ssh`) and cmd.exe, and preserves an existing
  `PROMPT_COMMAND`.
- **File-not-found notice.** The status bar now reports `File not found: ...` instead of
  silently doing nothing.
- **Open at line and column.** Clicking compiler output such as `C:\a\b\c.cpp:100:50`,
  `a/b/c.cpp:100:50` or `/home/me/c.cpp:100` opens the file with the caret at the given
  position, for both local and remote files. A name that contains a colon and digits still opens
  if no file exists without them.
- **Additional link handling.** A trailing comma is removed from a link, and `file://` links and
  clicked folders open with the default application.
- **Go to Line.** **Search → Go to Line...** (Ctrl+G, not in a terminal) shows an input box over
  the file. The caret follows the input (`:12` or `:12:5`). Enter confirms the position, and
  Escape restores the previous selection and scroll position.
- **Open files are restored with the session.** Kennel stores the open files in the workspace and
  reopens them on the next start, like sessions. Local files are restored immediately, and
  remote files are fetched in the background. Unsaved edits are not preserved.
- **Terminals are restored as well.** A plain terminal is restored with its last folder and the
  shell it runs, so it no longer asks for the shell again. Job runs are not saved.
- **Windows with a Linux shell.** A Linux folder reported by an `ssh` shell is no longer
  confused with a Windows path, and Kennel notes that the file may reside on another machine.
  msys2 and Git Bash paths (`/c/Users/...`) are mapped to drive paths.
- **File page toolbar.** Provides **Close**, **Save** and **Reload**. Reload re-reads the file
  and asks for confirmation if there are unsaved changes. Remote files are fetched in the
  background.
- **Cursor position in the status bar.** While a file is open, the review field of the status
  bar shows the line and column and, if text is selected, the number of selected characters.
- **File type icons.** Each open file shows an icon chosen by extension or name (for example,
  `CMakeLists.txt` is recognized as a CMake file and `.bashrc` as a shell script), taken from a
  dark or light set that matches the system appearance. The icons are shipped on all platforms.

### Faster page switching

- **Native notebook.** A native notebook replaces the custom page book, which was slow when
  switching between sessions.
- **Ctrl+Tab switcher.** The switcher is now modeless; the previous modal dialog left the
  application sluggish after the Ctrl key was released. It also uses a narrower list border and
  shows the folder of each page in the columns Name | Dir | Group.
- **Recent order.** The Ctrl+Tab order is updated whenever a page is shown, including pages
  selected from the File Explorer or the Activity pane.
- **No more tab-switching loops.** The notebook no longer selects a page merely because focus
  moved into it, which prevents two sessions from repeatedly taking the selection from each
  other.

### On Windows

- **Newer ConPTY.** Kennel ships `conpty.dll` and `OpenConsole.exe` next to `kennel.exe`, and
  includes them in the installer. Both come from the Microsoft.Windows.Console.ConPTY NuGet
  package (MIT license). The ConPTY built into Windows does not forward queries such as
  `OSC 11 ; ?` to the terminal, so applications like Claude Code could not detect the background
  color and chose a dark theme on a light one. If the two files are missing, Kennel falls back
  to the ConPTY of Windows.
- **Installer built on GitHub Actions.** A workflow builds Kennel with MSYS2 clang64 on a Windows
  runner, creates the installer with `./build.sh package` (Inno Setup), uploads it, and performs
  a silent installation as a smoke test. `build.sh` uses `mingw32-make` for all make invocations
  on Windows and names the CMake generator explicitly ("MinGW Makefiles"). Windows on ARM is
  disabled for now.
- **Color emoji and wide characters.** The terminal renders emoji in color (using Direct2D),
  including in the header, and wide characters (CJK and emoji) are no longer clipped by the
  OpenGL renderer.
- **wxWidgets 3.3.4.** `build.sh` builds the `v3.3.4` tag instead of the head of `master`.

## Additional Functionality

- **Ctrl+Click on a link.** A trailing `.` or `:` is removed from the link text, so
  `src/main.cpp:` opens the file.
- **New Terminal remembers the shell.** The list of shells starts with the most recently chosen
  shell, until the default shell is changed in Settings.
- **Start Agent remembers the last 10 folders** that were chosen.
- **Default theme.** The default terminal theme is now `one_dark`.
- **Code Reviewer icon.** A teal magnifying glass with a green check mark, legible on dark and
  light themes.
- **Reviewer header.** The Review Buddy terminal has its own header,
  `<Agent> (Review Buddy) | Reviewing changes`, which shows `Waiting` while the main agent
  addresses the comments.
- **New file types.** `.xrc`, `.wxcp`, `.jsonl` and `.workspace` files receive their own icon and
  language, and an ElastiCache icon has been added.
- **`build.sh --cmake`** forces the CMake configure step, and `WX_VERSION` selects the wxWidgets
  tag.
- **Terminal emulator updates.** `wxTerminalEmulator` received several updates: header, footer and
  progress bar strips, the OSC 7 / 9;9 working directory, OSC 7501, mouse tracking for tmux, and
  fixes for Shift+Tab and for focus handling on macOS.

## Bug Fixes

- **Slow tab switching.** See "Faster page switching".
- **Block cursor, selection and links on light terminal themes.** See "A theme for the whole
  application".
- **Missing row stripes on light themes** in lists.
- **Find on macOS.** Enter now works.
- **Remote files in the File Explorer.** A remote session folder stored as `$HOME/...` is now
  listed (it previously failed with "not a directory"), and a remote tree is displayed after a
  restore.
- **Closing a tab** now activates the previously used page (the next one in the recent order)
  instead of the adjacent tab or the first one. Closing a group behaves the same way.
- **Two sessions switching tabs indefinitely** after a restart with several sessions.

## Under the Hood

- **`MainNotebook`** is the page manager and the single source of truth for pages (the
  `SessionPage` and `FilePage` windows of the notebook). `PageRegistry`, `SessionGroup`,
  `TreeView`, `FlatView` and the other page lists, `TabHistory` and `AwaitingInputDetector` have
  been removed. `MainView` retains the theme, the pane layout and the side views.
- **Event-based communication between views.** `MainNotebook`, the File Explorer
  (`FileExplorerView`) and the Activity pane (`ActivityView`) are unaware of each other. The
  notebook sends `PageViewEvent`s (added, removed, renamed, shown and activity) and
  `FileEvent`s, and requests from a side view are returned on the `EventNotifier`.
  `ThemeManager` posts `wxEVT_THEME_APPLIED` whenever a theme is set.
- **`core/Account`** identifies the location of a session (this machine, WSL or a remote host),
  and the file providers have moved to `FileProvider`. `ParseFileLocation()` (in core) parses
  `file:line:column`, and `wsl::ToLinuxPath()` converts a Windows path to a WSL path.
- **`workspace.json`** now stores the open files (`files`). The groups, the sort preferences of
  the flat list, the splitter positions, `sidebarWidth` and `leftTab` are no longer saved.
- **`ReviewBuddyEvent`.** `ReviewBuddy` sends a custom event to its event target on every status
  change: `STARTED`, `WAITING_FOR_FINDINGS`, `ADDRESSING_FINDINGS`, `FINISHED`, `ERROR` (with
  the reason) and `STOPPED`. The request to close it is also a `ReviewBuddyEvent` (`CLOSE`), and
  `wxEVT_REVIEW_CHANGED` has been removed. `ReviewBuddyHeader` follows these events and renders
  the header, and `ReviewLoop::IsClean()` distinguishes a clean review from one that reached the
  round limit.
- **`toolbar::AddTool()`** and **`toolbar::ApplyStyle()`** (in `ToolBarUtils`) add the tools of
  every toolbar with a theme-aware disabled image and select the toolbar style from the
  application appearance.
- **`SearchBar`** is a `wxPanel` subclass shared by files and terminals.
- **`ApplyAppTheme()`** (in `AppTheme.h`) applies the saved theme through
  `wxApp::SetAppearance()`.
- **`BlockTimer`** is a new helper in the logger for measuring slow code.
- `wxTerminalEmulator` now provides `AddHeader()`, `AddFooter()`, `ShowProgressBar()`,
  `GetCurrentDir()` and the working directory event.

## Upgrade Notes

- **Rebuild wxWidgets.** `build.sh` now builds `v3.3.4`. Delete `.build-release/wxWidgets` and
  `.build-release/wxWidgets-install`, then run `./build.sh` again. On Windows, also re-run CMake
  with the same generator as before.
- **The application follows the terminal theme by default.** To change this, select **System**,
  **Dark** or **Light** under **Settings → Theme**.
- **The session tree and the flat list have been removed.** Use the tabs, Ctrl+Tab, the File
  Explorer and the Activity pane instead. The group colors and the tree's Group menu are also
  gone, and old preferences and groups in existing files are ignored. If the new panes are not
  visible, use **View → Reset Kennel Layout**.
- **The terminal no longer has a context menu.** Use the toolbar above the terminal. The shortcut
  for Find is Ctrl+F (Cmd+F on macOS).
- **New defaults.** The terminal theme is `one_dark` and the tabs are placed at the top. Choices
  that you have saved are preserved.
- **The session close button now asks.** Select **Ask**, **Archive** or **Close** under
  **View → Close Button of a Session**.
- **Ctrl+N is now New File** (previously Configure New Agent), and **Ctrl+M** toggles the panes.
- **The Windows installer** now also ships `conpty.dll` and `OpenConsole.exe`.
