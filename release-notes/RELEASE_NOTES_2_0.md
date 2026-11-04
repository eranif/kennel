# Kennel 2.0.0 Release Notes

*Covers changes since tag `1.4.0`.*

Kennel 2.0.0 makes the terminal page a tool you can work with: every session has a toolbar
(Restart, Review Buddy, Terminal, Copy, Paste, Find) instead of a right-click menu, a header that shows
what the session is, a search bar, and a thin progress bar. Review Buddy reports its progress
right in that header. The main window has a new layout: one notebook for all your sessions
and files, a **Side Bar** with a **File Explorer** and a **Git** view, and an **Activity** pane
that tells you which sessions are working, done or blocked. You can **archive** a session and
restore it later, find any command, page or action with **Goto Anything** (Ctrl+Shift+P), and a
first-time user gets the installed agents defined for them. The application can follow the
theme of the terminal, file links from the terminal follow `cd` and open at the right line,
open files come back with the session, and Windows gets a newer ConPTY, a newer wxWidgets and
an installer built on GitHub Actions.

## Major Improvements

### A new main window

- **One notebook for sessions and files** — the session tree, the flat list and the lists of
  files and terminals are gone. Every session, terminal and file is a tab of one notebook.
  The tabs are painted in the colors of the terminal theme (and follow it when you change
  it), a middle click closes a tab (and a close button on every tab is an option), and the tooltip of a
  tab shows its full title. **Ctrl+Tab** still switches between the pages, most recent first.
  - **Closing a tab shows the page you used before it** — the next one in the recent order,
    as Ctrl+Tab would, and not the tab next to it. The same goes for closing a group.
  - **Program status on the tabs** — a program that reports its state with OSC 7501 gets a
    spinner (working), a green check mark (done) or a hand (blocked, it needs you) on its tab.
    The mark goes away when you look at the page. The reviewer's terminal is followed too.
- **Side Bar** — a new pane with two tabs, the **File Explorer** and the **Git** view (see
  "Git in the Side Bar"). The tab you used last is selected at the next start, and
  **Settings** has the place of its tabs (top or bottom).
  - The side bar has one tree and one git view for each place: the folder of a session on
    this machine, in a WSL distribution, or on a remote host. They follow the page you are
    working in. A **terminal** has its own tree and git view, which follow the folder the
    shell is in.
  - Double click a file to open it. Typing in the tree selects the next entry that starts with
    the letters you type.
  - A **WSL** session shows the files of its distribution; a session on a **remote host**
    shows the files of the host. A remote tree connects in the background and shows
    `Connecting...` until it is ready, so the window does not wait for a slow host.
  - The tree shows hidden files, and the context menu has **Refresh**,
    **New Folder...**, **New File...**, **Delete...**, **Show in Finder / Explorer / File
    Manager**, **Open with Default App** and **Copy Path**. Delete is never the first entry
    and asks first. Select **several files** to open or delete them together.
- **Activity** — a new pane that lists the sessions whose program is working, done or
  blocked, with an icon (a turning spinner, a check mark, a hand). Blocked sessions come
  first, then done, then working. A working or blocked row stays while the program is in
  that state, also for the page you are looking at. A done row goes when you are in that
  session. Click a row to show the session.
- **Tab groups** — the tabs of the sessions of a group sit together under a header with the
  name of the group (Files and Terminals have one too). **Move To Group** and **Rename Group**
  update the tabs at once, and **View → Group Tabs by Session Group** turns it off. The order of
  the tabs is saved and comes back at the next start.
- **Tab options** — **Settings** has the **position of the tabs** (top, bottom, left or
  right), the **colour of the tab outline**, and a **Close button on the tab** option. The tabs
  have the look of a browser, a file tab shows the folder and the name (`c/file.cpp`) and the
  tooltip shows the full path, and a file with unsaved changes has an orange dot on its icon.
  The tab position is also in the menu of the tab bar: right click on the empty bar, **Tab
  Position**. Tabs are on top by default.
- **View menu** — **Side Bar** and **Activity** show or hide a pane, and **Toggle All Views**
  (**Ctrl+M**, also in a terminal; the Ctrl key on macOS too) hides every pane but the pages, or
  brings them back.
- **View → Reset Kennel Layout** — puts the panes back to the layout they have at the first
  start. The layout of the panes is saved between runs; a saved layout that does not know a
  new pane shows that pane again. The side panes start 300 DIP wide, and float at 400x600 DIP.
- **The application can follow the terminal theme** — see "A theme for the whole application".
- **Faster remote files** — Kennel keeps its SFTP connections open (one pool for each host)
  instead of making a new SSH connection for every call. Opening a remote file no longer
  waits for a new handshake, which took seconds on a host far away.

### Git in the Side Bar

- **Git view** — the second tab of the Side Bar shows the git status of the place you work in
  (a local folder, a WSL distribution or a remote host, over SSH): the branch, and a list of the
  files with their status and a file type icon. It is refreshed when you save a file and after
  each action. A double click shows the **diff** in a read-only page that is not kept in the
  session (the editor colours the added and deleted lines); the context menu has **Show Diff**
  and **Open File**.
- **Actions** — a toolbar with one button for each action. You can work on several files at
  once.
  - **Add** (the **+** button) stages the selected files, **Reset** puts the selected files
    back, and **Reset Repo** resets the whole repository (after asking).
  - **Commit** opens a message box with **Amend** and **Signoff** (Signoff is remembered) and
    runs the commit in the background, with the errors reported to you.
  - **Log** asks for the number of lines (100 by default) and shows the log in an editor.
    **Blame** shows the blame of the selected file.
  - **Push** and **Pull** run in a terminal named `Git (<place>)` that Kennel opens (or uses
    again), on the same host, so passwords are asked where git asks them.

### Archive a session

- **Archive** — a session can be archived instead of closed: its tab goes away, and it is kept
  in `workspace.json`, with the date. Use **Archive** in the menu of the tab. If the agent is
  working, Kennel asks first. Terminals are not archived.
- **File → Archived Sessions...** lists the archived sessions (name, agent, folder, date) with
  a filter box. Sort by any column; the newest is first. **Restore** starts the agent again with
  its resume option (it gets a number when its name is taken) and the dialog stays open so you
  can restore more than one. **Delete** removes the entry for good.
- **The close button asks** — closing an agent session (the close button of the tab, or
  **Close** in its menu) asks **Archive**, **Close** or **Cancel**. **Do not ask again** keeps
  your answer; **View → Close Button of a Session** (Ask, Archive, Close) changes it later.

### Goto Anything

- **Ctrl+Shift+P** (also **Search → Goto Anything...**, also in a terminal) opens a box with a
  search field and a list; there are no buttons. Type words to filter the list, move with the
  arrow keys or Page Up / Page Down, press Enter to choose and Escape to close.
- **What it finds** — the actions of the page that is showing (**Restart Session** or **Restart
  Terminal**, **Start Review Buddy with ...** for each agent that can review the session, and
  **Close Review Buddy**), the open pages (`Select Session: ...`, `Select Terminal: ...`,
  `Select File: ...`), and **every enabled item of the menu bar**, with its keyboard shortcut.
  The **Edit** menu is in the list only while a file is showing.

### The first start

- **Agents are found for you** — the first time Kennel starts, it looks in the background for
  `claude`, `codex` and `kiro-cli` on this machine and defines an agent for each, with the right
  icon. It is done once (`localAgentsScanned` in `.persist.json`) and never shows a dialog. A
  tool that is defined already is not added again, and an upgrade from an older version does not
  scan.
- **WSL and SSH hosts** — then Kennel asks, once, in one dialog, whether to look on your WSL
  distributions (on Windows) and on the hosts of your SSH config. The distributions are checked,
  the hosts are for you to choose. The search runs in the background: the status bar shows
  where it is, ssh never asks for a password (`BatchMode`), and a summary tells what was added.
  Agents get a name such as `Claude (WSL: Ubuntu)` or `Codex (myhost)` and the WSL or remote
  icon.
- **A bigger window** — a new user's window is 80% of the screen, centered.

### The editor and the files

- **Edit menu** — in the menu bar only while a file is showing, so a terminal keeps its keys
  (Ctrl+R, Ctrl+C, Ctrl+D...): **Close Editor** (Ctrl+W), **Reload File**, **Undo**, **Redo**,
  **Copy**, **Paste**, **Select All**, **Delete Line** and **Duplicate Line**, and the options
  **Wrap Lines**, **Highlight Braces** and **Highlight Indent Lines** (saved; open editors follow
  a change). Wrap Lines is also a tool on the toolbar of a file.
- **New File and Open File** — **File → New File** (Ctrl+N) opens an empty `Untitled` editor
  (Save asks for the file name) and **File → Open File** (Ctrl+Shift+O) opens a file. **Rename
  Session** (F2) is in the File menu.
- **Unsaved changes are not lost** — when Kennel closes, each file with changes asks **Save**,
  **Discard** or **Cancel**, and Cancel keeps the application open.
- **Diff and Rust** — `.diff` and `.patch` files are coloured, and Rust is a supported language.
- **The caret line is centred** when a search match or Go to Line moves the caret.

### A toolbar and a header for every terminal

- **Terminal toolbar** — a toolbar above every session has **Restart**, **Review Buddy**,
  **Terminal**, **Copy**, **Paste** and **Find**. The terminal has no right-click menu any more: the
  actions are in the toolbar. (The old menu's **Clear buffer** entry is gone with it.)
  - **Copy** copies the selected text. When nothing is selected, it copies the text the
    program in the terminal copied itself (for example tmux, or a TUI such as Claude Code),
    all of it and not only the first line. It is disabled when there is nothing to copy.
  - **Paste** and **Find** work on the terminal that has the focus (the agent's or the
    reviewer's).
  - **Terminal** opens a terminal named `<Session> | Terminal` on the same host and in the same
    folder as the session (local, WSL or remote). Press it again to go back to that terminal.
  - **Review Buddy** is a drop-down. With no review open, it lists the agents on the same host
    to review with ("Review with ..."), or says why a review is not possible (no working
    directory, not a git repository). With a review open, it shows the state and offers
    **Send the Request Again**, **Stop the Review**, **Open the Latest Review** and
    **Close Review Buddy**. It is disabled for plain terminals and scheduled jobs.
- **Header above the terminal** — the agent's terminal shows
  `Agent Name | Title | Working Directory` in a strip above it. It follows the terminal's
  title and the folder the shell is in now (before the shell reports one, the folder the
  session started in). A plain terminal shows `Terminal`. On a dark theme the text is yellow
  on a strip darker than the terminal.
- **Review Buddy in the header** — while a review is open, the header shows only the state of
  the review: a braille spinner (⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧) while somebody is working, for example
  `⠹ Waiting for review (round 1/5)` or `⠼ Addressing comments (round 2/5)`. The end of the
  review has a mark in front of the status: 👍 the review is clean, 👎 it stalled or failed,
  🔴 the round limit was reached with findings still open. The header goes back to
  `Agent | Title | Folder` when you close the Review Buddy.
- **Progress bar** — a thin bar above the terminal shows the progress a program reports with
  the OSC 9;4 sequence (the ConEmu / Windows Terminal progress bar) or with OSC 7501 (the
  program status protocol): a percent bar, or a moving bar when there is no percent. It is
  hidden when there is no progress, and it gives the terminal only a few pixels. Try it with
  `printf '\e]9;4;1;40\a'` (and `\e]9;4;0;0\a` to hide it).
- **Toolbar icons for every theme** — new blue Copy, Paste, Find, Save, Close and Reload
  icons, and disabled toolbar buttons that fit the theme: before, the disabled image was
  shiny and white on a dark theme.

### A theme for the whole application

- **Follow terminal theme, System, Dark or Light** — a new **Theme** choice in **Settings**
  sets the look of the application. It is saved in `.persist.json` (`appTheme`) and applied
  when Kennel starts and when you confirm the settings. The default is **Follow terminal
  theme**: the application is dark or light as the background of the terminal theme is, also
  while you pick a terminal theme.
- **Toolbars follow the theme** — a light theme gets a plain toolbar background and a dark
  theme the default one. A change in Settings updates the toolbars of the open sessions and
  files at once.
- **A dark and a light set of icons** — the icons of the toolbars, menus and lists (agent,
  terminal, job, code reviewer, reload, save, close, find, copy, paste, up, home) have a shade
  of grey for each theme and follow the theme of the application.
- **Stripes on a light theme** — the alternating row colour of the lists was a lightened
  window colour, which does nothing on white. A light background now gets darker rows, so the
  stripes show.
- **The editor** — the line numbers and the comments are readable on any theme (a dimmed text
  colour and a muted green, with a minimum contrast), the line of the caret is highlighted,
  and the caret is wider.
- **Terminal themes fit light backgrounds** — many theme files have no cursor colour, and the
  default is white: the block cursor could not be seen on a light theme. Now:
  - a theme without a cursor colour uses its foreground colour;
  - the selection (`[colors.selection]`) and search match (`[colors.search.matches]`) colors
    of a theme are used, and when a theme sets only one color of a pair (for example
    `CellForeground`), a readable color is picked for the other one;
  - the link color is the theme's own blue (the bright one on a dark background);
  - a color value with a comment after it (`'#303446' # base`) is no longer skipped.

### Search in files and in terminals

- **A search bar instead of a dialog** — Ctrl+F (Cmd+F on macOS), or **Find** in the Search
  menu or in the terminal toolbar, shows a search bar at the bottom of the page, below the
  editor or the terminals. It searches as you type. Enter or the down arrow finds the next
  match, Shift+Enter or the up arrow the previous one, and Escape closes it. The selected
  text is the text to look for.
- **Files** — the search bar also has **Full word** and **Case sensitive**, and a second
  line with **Replace with**, **Replace** and **Replace all** (one undo step). The Find and
  Replace dialog and the Replace menu entry are gone; Tab goes from the find text to the
  replace text, and back.
- **Terminals** — the bar has the search text and the arrows. Closing it clears the
  highlights. This also fixes Find on macOS, where Enter in the old dialog did nothing.
- **F3 and Shift+F3** — find the next and the previous match, whatever the direction is.
  A terminal keeps F3 for the program in it (such as `mc` or `htop`) until you have a
  search. Ctrl+F no longer reaches the program in the terminal on Windows and Linux, so
  readline's "forward one character" does not work there.

### Files opened from the terminal

- **Relative paths follow `cd`** — Kennel asks the shell where it is (OSC 7, and OSC 9;9 for
  cmd.exe). A relative path you click is looked up in that folder first, then in the folder the
  session started in, and in the home folder only when there is no other. This works in plain
  terminals, in WSL and over SSH. The shell integration is added by the terminal for bash
  (also over `ssh`) and cmd.exe, and an existing `PROMPT_COMMAND` is kept.
- **A notice when the file is not found** — the status bar says `File not found: ...`
  instead of doing nothing.
- **Open at the line and the column** — a click on compiler output such as
  `C:\a\b\c.cpp:100:50`, `a/b/c.cpp:100:50` or `/home/me/c.cpp:100` opens the file with the
  caret at that place, for local and remote files. A name that has a colon and digits in it
  still opens when the file without them does not exist.
- **More links** — a trailing comma is cut from the link, `file://` links and clicked folders
  open with the default application.
- **Go to Line** — **Search → Go to Line...** (Ctrl+G, not in a terminal) shows a box on top
  of the file. The caret follows what you type (`:12` or `:12:5`), Enter keeps it, and Escape
  puts back the selection and the scroll position.
- **Open files come back with the session** — the files you have open are saved with the
  workspace and opened again at the next start, like the sessions. Local files come back at
  once, remote files are fetched in the background. Unsaved edits are not kept.
- **Terminals come back too** — a plain terminal is restored with the folder it was in and
  the shell it runs, so it does not ask for the shell again. Job runs are not saved.
- **Windows with a Linux shell** — a Linux folder of an `ssh` shell is not mixed with a
  Windows path any more. Kennel says that the file may be on another machine. msys2 and Git
  Bash paths (`/c/Users/...`) are mapped to drive paths.
- **A toolbar on file pages** — **Close**, **Save** and **Reload**. Reload reads the file
  again, after asking when there are unsaved changes; remote files are fetched in the
  background.
- **Position in the status bar** — while a file is open, the line, the column and, with a
  selection, the number of selected characters are in the review field of the status bar.
- **A file type icon for each open file** — chosen by the extension or the name (for
  example `CMakeLists.txt` is a CMake file, `.bashrc` a shell script), from a dark or a light
  set that fits the system appearance. The icons are shipped on all platforms.

### Switching pages is faster

- **A native notebook** replaces the custom page book, which was slow when switching sessions.
- **The Ctrl+Tab switcher** is modeless: the modal dialog left the application lagging after
  the Ctrl key was released. It also keeps its list border narrower, and shows the folder of
  each page in the columns Name | Dir | Group.
- **The recent order** (the Ctrl+Tab order) is updated whenever a page is shown, also when
  you pick it in the File Explorer or in the Activity pane.
- **No more tab-switching loops** — the notebook no longer selects a page just because the
  focus went into it, so two sessions cannot pull the selection back and forth.

### On Windows

- **A newer ConPTY** — Kennel ships `conpty.dll` and `OpenConsole.exe` (from the
  Microsoft.Windows.Console.ConPTY NuGet package, MIT) next to `kennel.exe`, also in the
  installer. The ConPTY built into Windows does not pass queries such as `OSC 11 ; ?` to the
  terminal, so apps like Claude Code could not find the background color and picked a dark
  theme on a light one. Without the two files, Kennel falls back to the ConPTY of Windows.
- **An installer built on GitHub Actions** — a workflow builds Kennel with MSYS2 clang64 on a
  Windows runner, creates the installer with `./build.sh package` (Inno Setup), uploads it and
  installs it silently as a smoke test. `build.sh` uses `mingw32-make` for every make on
  Windows and names the CMake generator ("MinGW Makefiles"). Windows on ARM is off for now.
- **Colour emoji and wide characters** — the terminal draws emoji with their own colors
  (with Direct2D), also in the header, and wide characters (CJK, emoji) are no longer
  clipped in the OpenGL renderer.
- **wxWidgets 3.3.4** — `build.sh` builds the `v3.3.4` tag instead of the tip of `master`.

## Additional Functionality

- **Ctrl+Click on a link** — a trailing `.` or `:` is cut from the link text, so
  `src/main.cpp:` opens the file.
- **New Terminal remembers the shell** — the list of shells starts with the one you chose last
  (until you change the default shell in Settings).
- **Start Agent remembers the last 10 folders** you chose.
- **Default theme** — the default terminal theme is `one_dark`.
- **A Code Reviewer icon** — a teal magnifying glass with a green check mark, readable on dark
  and light themes.
- **The header of the reviewer** — the terminal of Review Buddy has its own header,
  `<Agent> (Review Buddy) | Reviewing changes` (or `Waiting` while the main agent addresses the
  comments).
- **New file types** — `.xrc`, `.wxcp`, `.jsonl` and `.workspace` files get their icon and
  language, and there is an ElastiCache icon.
- **`build.sh --cmake`** forces the CMake configure step. `WX_VERSION` selects the
  wxWidgets tag.
- **Terminal and tool changes** — `wxTerminalEmulator` was updated several times: header,
  footer and progress bar strips, OSC 7 / 9;9 working directory, OSC 7501, mouse tracking for
  tmux, and fixes for Shift+Tab and the focus on macOS.

## Bug Fixes

- **Slow tab switching** — see "Switching pages is faster".
- **Block cursor, selection and links on light terminal themes** — see "A theme for the whole
  application".
- **No stripes on a light theme** in the lists.
- **Find on macOS** — Enter now works.
- **Remote files in the File Explorer** — a remote session folder stored as `$HOME/...` is
  listed now ("not a directory" before), and a remote tree is shown after a restore.
- **Closing a tab** shows the page you used before it (the next one in the recent order), not
  the tab next to it or the first one. Closing a group does the same.
- **Two sessions switching tabs forever** after a restart with several sessions is fixed.

## Under the Hood

- **`MainNotebook`** is the page manager and the only source of truth for the pages (the
  `SessionPage` and `FilePage` windows of the notebook). `PageRegistry`, `SessionGroup`,
  `TreeView`, `FlatView` and the other page lists, `TabHistory` and `AwaitingInputDetector`
  are removed. `MainView` keeps the theme, the layout of the panes and the side views.
- **Views talk with events only** — `MainNotebook`, the File Explorer (`FileExplorerView`) and
  the Activity pane (`ActivityView`) do not know each other. The notebook sends
  `PageViewEvent`s (added, removed, renamed, shown, activity) and `FileEvent`s; what the user
  asks in a side view comes back on the `EventNotifier`. `ThemeManager` posts
  `wxEVT_THEME_APPLIED` when a theme is set.
- **`core/Account`** names the place of a session (this machine, WSL or a remote host), and
  the file providers moved to `FileProvider`. `ParseFileLocation()` (core) parses
  `file:line:column`, and `wsl::ToLinuxPath()` converts a Windows path to WSL.
- **`workspace.json`** has the open files (`files`). The groups, the sort preferences of the
  flat list, the splitter positions, `sidebarWidth` and `leftTab` are not saved any more.
- **`ReviewBuddyEvent`** — `ReviewBuddy` sends a custom event to its event target whenever its
  status changes: `STARTED`, `WAITING_FOR_FINDINGS`, `ADDRESSING_FINDINGS`, `FINISHED`,
  `ERROR` (with the reason) and `STOPPED`. The request to close it is a `ReviewBuddyEvent` too
  (`CLOSE`). `wxEVT_REVIEW_CHANGED` is gone. `ReviewBuddyHeader` follows these events and draws
  the header; `ReviewLoop::IsClean()` tells a clean review from the round limit.
- **`toolbar::AddTool()`** and **`toolbar::ApplyStyle()`** (`ToolBarUtils`) add the tools of
  every toolbar with a theme-aware disabled image, and choose the style of a toolbar from the
  appearance of the application.
- **`SearchBar`** is a `wxPanel` subclass, used by files and terminals.
- **`ApplyAppTheme()`** (`AppTheme.h`) applies the saved theme through
  `wxApp::SetAppearance()`.
- **`BlockTimer`** helper in the logger, for measuring slow code.
- `wxTerminalEmulator` now provides `AddHeader()`, `AddFooter()`, `ShowProgressBar()`,
  `GetCurrentDir()` and the working directory event.

## Upgrade Notes

- **Rebuild wxWidgets.** `build.sh` now builds `v3.3.4`. Delete `.build-release/wxWidgets` and
  `.build-release/wxWidgets-install`, and run `./build.sh` again. On Windows, also run CMake
  again with the same generator as before.
- **The application follows the terminal theme by default.** Choose **System**, **Dark** or
  **Light** in **Settings → Theme** to change it.
- **The session tree and the flat list are gone.** Use the tabs, Ctrl+Tab, the File Explorer
  and the Activity pane. The group colours and the Group menu of the tree are gone with them;
  old preferences and groups in your files are ignored. If the new panes are not shown, use
  **View → Reset Kennel Layout**.
- **The terminal has no context menu.** Use the toolbar above the terminal. The shortcut for
  Find is Ctrl+F (Cmd+F on macOS).
- **New defaults.** The terminal theme is `one_dark` and the tabs are on top. Your saved choices
  are kept.
- **The session close button asks.** Choose **Ask**, **Archive** or **Close** in **View → Close
  Button of a Session**.
- **Ctrl+N is New File** (it was Configure New Agent), and **Ctrl+M** toggles the panes.
- **The Windows installer** now also ships `conpty.dll` and `OpenConsole.exe`.
