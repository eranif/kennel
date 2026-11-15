# Kennel 3.0.0 Release Notes

*Covers changes since tag `2.0.0`.*

Kennel 3.0.0 runs **sessions in tmux**. An agent, or a plain terminal, runs in a tmux session on
its machine (this computer on Linux and macOS, WSL, or a host reached over ssh), so it continues to
run when Kennel is closed or the connection is lost, and the next start attaches to it again. The
status icons of a tab continue to work through tmux with the help of **tmux-shim**, which Kennel
installs on the machine after confirmation.

The release also adds **Find in Files**: a search of a folder of this computer, of a WSL
distribution or of a host reached over ssh, where a match opens in the editor with the text
selected. The results are shown in a new **Output** pane. Kennel installs the search program
(ripgrep) on the target machine after confirmation. In addition, the release refines the window
introduced in 2.0: the main toolbar has a new design, an **Archive** button, and the shortcut of
each button in its tooltip; the tab height is a setting that is applied immediately; **Review
Buddy** has its own menu and a shortcut; and **Goto Anything** and the page switcher open at the
top of the window, with rounded corners and a shadow.

Highlights:

- **Sessions in tmux**: agents and terminals outlive the window. **Close** stops a session,
  **Archive** keeps it running, and a tab whose terminal has ended while its tmux session is
  still running is shown as **Detached**. Machines are prepared in the background, and questions
  are shown in a message bar on the tab instead of a dialog.
- **tmux-shim** preserves the working, done and blocked icons of a tab in tmux sessions.
- **Remote hosts do not block the window**: the folder browser and the agent dialog.
- **Find in Files** (Ctrl+Shift+F) for this computer, WSL and ssh hosts, with the matches in the
  new **Find in Files** tab of the Side Bar (Ctrl+`).
- **ripgrep is installed by Kennel**, in the same version everywhere, after confirmation.
- A new **main toolbar** with grouped buttons, a new **Archive** button, and the shortcut of each
  button in its tooltip.
- A new **Tab Height** setting with four sizes (Short, Small, Medium, Tall), applied live to the
  notebook and to the Side Bar, and available from the context menu of the tab bar.
- A **Review Buddy menu** and the shortcut **Ctrl+Shift+R** (Cmd+Shift+R on macOS).
- **Goto Anything** and the page switcher open at the top of the window.
- Find starts with the selected text, including in a terminal.

## Major Improvements

### Sessions in tmux

- **Agents and terminals run in tmux** on the machine where they run: this computer (Linux and
  macOS), a WSL distribution, or a host reached over ssh. The terminal in Kennel is a view of the
  tmux session, so the agent continues to run when Kennel is closed, the application terminates
  unexpectedly, or the connection is lost. A Windows shell on this computer does not use tmux.
  tmux is enabled by default.
- **Requirement:** tmux 3.3 or newer, installed with the package manager of the machine
  (`apt install tmux`, `brew install tmux`). Kennel does not install tmux.
- **Restore attaches.** At the next start, each saved session attaches to its tmux session. If it
  no longer exists (for example after the machine was rebooted), a new one is started with the
  resume option of the agent, as before. The tmux session is named `kennel-<group>-<session>`
  (spaces, dots and colons are replaced with `-`), and the name is retained when the session is
  renamed.
- **Close stops, Archive keeps.** Closing a session, a group or all sessions ends their tmux
  sessions. **Archive** leaves the tmux session running, and **Restore** attaches to it. Deleting
  an archived session stops it. Closing Kennel only detaches.
- **Detached.** When the terminal of a session ends while its tmux session is still running (the
  user pressed `Ctrl+b` `d` and ran `exit`, the connection was lost, or the shell ended before the
  machine was ready), the tab remains and shows this state. **Restart** (`F5`) attaches again. If
  the tmux session no longer exists, the tab is closed, as before.
- **Plain terminals.** A terminal runs its login shell (`$SHELL`) in tmux. A terminal on a remote
  host logs in with ssh again when it is restored, and attaches to its tmux session on the host.
  The terminal opened from the toolbar of a session behaves the same way, on the host of the
  session.
- **A choice per session.** The **Start Agent** dialog has a **Run in tmux** box, shown for an
  agent that runs where tmux is available, with the default from **Settings → Tmux**.
- **Identification.** The icon of the tab of a session in tmux shows the tmux logo
  (`assets/tmux.svg`) in its lower right corner, and the tab of a **Detached** session has a
  yellow dot. The status bar shows `tmux: <name>` next to the host (the tooltip has the full
  text).
- **Tab menu.** A tab in tmux has **Kill tmux session and close**, which ends the tmux session and
  closes the tab, without the question about archiving.
- **Settings → Tmux.** The default for **Run sessions in tmux**, and the tmux commands (one per
  line) that are set for the sessions of agents and of terminals, each with **Reset to Default**.
  A change applies to the sessions that start afterwards.
- **The archive list shows the tmux state.** **Archived Sessions** has a **tmux** column
  (*Running*, *Ended* or *Unknown*) for each archived session. The state is determined in the
  background, with one query for each machine.
- **Separate tmux servers.** Agents use the tmux server `-L kennel` and terminals use
  `-L kennel-term`. Both are started without a configuration file, so a personal tmux
  configuration is not affected. The status line is off and the size follows the Kennel terminal.
  The defaults are `mouse on` (the wheel scrolls the history), vi keys, the clipboard (OSC 52)
  and, for agents, `allow-passthrough` (adapted to the version of tmux).

### Machines are prepared in the background

- **Every session has its terminal immediately.** A session that is not in tmux starts its program
  at once, and a tmux session waits until its machine is ready. When the workspace is restored,
  Kennel collects the machines (each one once, regardless of the number of sessions it holds) and
  checks all of them concurrently on worker threads, so a slow or unreachable host delays only its
  own sessions.
- **No dialogs: a message bar on the tab.** Each session shows its own bar, so several sessions
  can present questions at the same time:
  - *tmux-shim is not installed on X*: **Download** (performed once for all sessions of the
    machine, which then continue) or **Close session**.
  - *tmux is not installed on X*, *older than 3.3*, or *the host cannot be reached*: the bar
    describes the required action, and **Restart** checks the machine again. Alternatively,
    **Close session**.
- **Close session** closes the tab and ends its tmux session. Nothing is removed or stopped
  without this action: a session on an unavailable machine keeps its tab and remains in the
  workspace.
- A new session, **Restart**, and **Archive**/**Restore** use the same preparation. A machine
  that is ready is not checked again.

### tmux-shim

- **Activity status through tmux.** [tmux-shim](https://github.com/eranif/tmux-shim) 1.1.1 wraps
  the progress (OSC 9;4), status (OSC 7501) and folder (OSC 7) sequences of a program in the
  passthrough of tmux, so the spinner, the check mark and the pulse on the tab behave as without
  tmux. It was tested with Claude Code, Codex and Kiro.
- **Installed after confirmation** into `~/.kennel/bin` on the machine, for Linux x86_64 and
  aarch64, with a pinned version and a SHA-256 checksum (as for ripgrep). On a machine that it
  does not support (macOS), or where it cannot be installed, the session runs in tmux without it
  and shows no activity status.

### Remote hosts do not block the window

- **The folder browser** lists a remote folder on a separate thread. A host that cannot be reached
  results in a message.
- **Edit Agents** lists the programs of the host in the background when a host is selected, and
  has a **Refresh** button next to the list of executables to query the host again.

### Find in Files

- **Search a folder on any machine.** **Search → Find in Files...** (Ctrl+Shift+F, also while a
  terminal has the focus) requests the text to find, the folder, and the options: **Regex**,
  **Match case**, **Whole word**, and the files to **Include** and **Exclude** (globs separated by
  commas, for example `*.cpp, *.hpp`). It works on this computer, in a WSL distribution, and on a
  host that runs Linux or macOS and is reached over ssh.
- **Context-aware defaults.** The dialog searches for the text selected in the page that is
  showing (as Find does, including in a terminal). The folder is that of the session. For a file,
  it is the **closest workspace**: the working folder of a session, or the folder of its file
  explorer, that contains the file on the same machine; without one, the folder of the file. The
  home folder of this computer is never treated as a workspace. The pattern, the options, the
  globs and the folder are remembered for the next use.
- **Results in the Side Bar.** A search shows the **Find in Files** tab of the Side Bar.
  **View → Toggle SideBar** (Ctrl+`, Control+` on macOS) shows or hides the Side Bar. A toolbar provides **Stop** and **Clear**. The
  first line states what was searched, where and with which options. The matches are grouped by
  file, with the line number and the matched text in a distinct color, and the last line reports
  the number of matches (a search stops at 5000). A search displays its matches while it runs,
  including over ssh (the output of the remote `rg` is streamed through an ssh channel), and
  **Stop** closes the channel. A new search replaces the previous results.
- **A click opens the match.** One click (or Enter) on a match opens the file at that line, with
  the matched text selected and the line in the middle of the editor, for local, WSL and ssh
  files. A file that is already open is shown and is not fetched again (also over ssh): a click on
  a match, a path in a terminal and the other places that open a file use the same function,
  `MainNotebook::FindOrOpenFile()`. The text in the results cannot be selected, because a click
  opens the match.

### ripgrep installation

- **Kennel installs ripgrep 15.2.0 on the machine that is searched**, into `~/.kennel/bin`, on
  Windows, macOS and Linux, in WSL and on ssh hosts. The version is the same everywhere, and Kennel
  never uses another `rg` found on the machine. A host that runs Windows over ssh is not
  supported.
- **Confirmation first.** When ripgrep is missing (or is another version), the dialog asks
  **Download and Install ripgrep?** and states the destination. Nothing is downloaded or written
  before confirmation; **No** closes the dialog.
- **The download is verified.** It comes from the ripgrep release on GitHub and is compared with
  a SHA-256 checksum that is part of Kennel. A file that does not match is discarded and nothing
  is written. The program is written under a temporary name and then renamed, so a failed
  installation never leaves a file that appears to be installed. A remote machine does not need
  internet access: this computer downloads the file and uploads it.

### The file explorer follows a file without waiting

- **A file that is opened is shown in the file explorer immediately.** The folders on the way that
  are not listed yet are listed on a separate thread, so a click on a match in a folder that was
  not open no longer freezes the window (a large folder, a WSL share). Only the file that is
  showing is selected, and it is scrolled into view.
- **Close All Files is lighter.** The file explorer does not follow every page that is shown while
  the files are closed one after the other.

### The main toolbar

- **A new design.** The toolbar on the left of the window matches the tab bars: a flat bar in the
  color of the theme, with the buttons of a group placed together on a rounded pill that appears
  sunk into the bar, and a gap between groups. Tools are larger, hovered and pressed buttons are
  highlighted with a circle, and a button with a drop-down shows its arrow under the icon. The
  toolbar is the same on every platform: on macOS it is no longer drawn by the generic
  `wxToolBar`.
- **An Archive button.** It opens **Archived Sessions**, where archived sessions are restored or
  deleted. It is placed in a group with **Manage Jobs**, between the buttons that start a session
  and the buttons of the agents. The shortcut is **Ctrl+Shift+A** (Cmd+Shift+A on macOS), also
  while a terminal has the focus.
- **Tooltips show the shortcut.** A button of the main toolbar that has a keyboard shortcut shows
  it on its own line below its name, for example **Start a New Agent** and **Ctrl+T**. The
  buttons concerned are **Start a New Agent** (Ctrl+T), **Start a New Terminal** (Ctrl+E) and
  **Archive** (Ctrl+Shift+A); **Manage Jobs** and the buttons of the agents have no shortcut. The
  shortcut is taken from the menu item that performs the same action, so the key is defined in a
  single place.
- The toolbars at the top of a session and of a file keep a plain background.

### Tab height

- **A new setting, Settings → Tab Height**, with **Short**, **Small**, **Medium** and **Tall**.
  The choice is applied during selection, and **Cancel** restores the previous height.
- **The Side Bar follows.** The tabs of the File Explorer and of the Git view have the same
  height as the tabs of the notebook.
- **The context menu of the tab bar has a Tab Height submenu**, next to **Tab Position**. A
  choice made there is saved and applied to both tab bars.
- The four heights have the same size on every platform: the padding of a tab is 4, 6, 8 and 10
  DIPs. See "Upgrade Notes".

### Review Buddy

- **A Review Buddy menu** in the menu bar, between **Jobs** and **Settings**. It is shown only
  while an agent session is showing, and not for a terminal or a file. It contains **Start Review
  Buddy...**, the current activity of the review buddy, **Send the Request Again**, **Stop the
  Review**, **Open the Latest Review** and **Close Review Buddy**.
- **Ctrl+Shift+R** (Cmd+Shift+R on macOS) starts a review buddy, also while the terminal of the
  agent has the focus. On Windows and Linux, where the key cannot be used (a terminal, a scheduled
  job, or a review buddy that is already open), it is passed on to the terminal.
- **Start Review Buddy...** shows a list of the agents that can review the session, instead of one
  menu entry for each agent. When there is only one, the review starts immediately. If a review
  cannot be started (the folder is not a git repository, or no agent runs on the same host as
  the session), a message states the reason.
- The Review Buddy button is no longer in the toolbar of a plain terminal. The menu of the button
  opens below it and no longer covers it.

### Goto Anything and the page switcher

- **Both open at the top of the window**, centered, with rounded corners and a shadow (on
  Windows and macOS).
- **The page switcher is a popup.** It takes half the width and a quarter of the height of the
  window, and does not take the activation from it. It closes when Ctrl is released, when Escape
  is pressed, or when a click is made outside of it.
- **Goto Anything** is a dialog styled like that popup, placed just below the title bar of the
  window.
- On macOS the popups are translucent, with a blurred background.
- The shortcuts in Goto Anything are shown with the key names of the platform (for example Cmd on
  macOS).

### Find

- **Find starts with the selected text.** When Ctrl+F (Cmd+F on macOS) is pressed, the search box
  starts with the text selected in the editor or in the terminal. When several lines are
  selected, the first non-empty line is used. Previously, a selection of several lines was
  ignored.
- **In a terminal, text selected by a full-screen program takes precedence.** Such a program
  selects text itself and copies it with an OSC 52 sequence; the terminal has no knowledge of
  what it selects. The text selected with the mouse comes next.

### Other improvements

- **OSC 52 copy requests are handled.** A program in the terminal can copy text to the clipboard.
  The copy is performed only while that terminal has the keyboard focus, so a program in a tab in
  the background cannot change the clipboard.
- **Diffs have an icon.** The tabs of the diffs of the Git view (`.diff`, `.staged.diff`) and the
  files with the extensions `diff` and `patch` show a dedicated icon: a green plus and a red
  minus.
- **The context menu of the Git view starts with Open File**, followed by a separator and the
  other entries. For a single file it also has **Open with Default App** (not for a file on a
  remote host, which this computer cannot open).
- **On macOS** the tab bar of the notebook shows a blurred view of the desktop where it has no
  tab.
- **The Job dialog** has a tidier layout: **Keep terminal open** and **Enabled** are on one line,
  and the extra separator is removed.
- **Switching the Side Bar between sessions is lighter.** It selects a page of the File Explorer
  and of the Git view only when it is not the one already showing, and holds the window only for
  the two switches, not for the git refresh and the remote connection that follow.

## Fixes

- **WSL: the home folder was not found.** A path that starts with `~` in a WSL distribution could
  not be resolved, because the command that asks the distribution for its home folder was started
  without quotes and printed nothing.
- **Windows: folders and names that are not ASCII.** Kennel starts programs with their command
  line in UTF-16, so a folder such as `C:\Users\José\project` or `D:\проект` is passed correctly
  to `git` and to the other programs that Kennel runs. Previously, the system code page could
  corrupt it.
- **macOS build:** Kennel links the Cocoa framework that the new popups require, and the build no
  longer prints the `NSOpenGLContext` deprecation warning of the wxWidgets headers.

## Internal Changes

- **`KennelToolBarArt`** draws the main toolbar (and the horizontal toolbars of the pages, with a
  compact variant). `toolbar::ApplyStyle()` leaves the background of a toolbar with this art as
  it was made.
- **`wxEVT_APP_TAB_HEIGHT`** (in `core/AppManager.h`) requests that the tab bars show a tab height.
  The settings dialog and the menu of the tab bar send it; the notebook and the Side Bar listen.
  `TabPadding.h` defines the padding of each height. In the prefs file, `tabHeight` keeps its
  numbers (0 Short, 1 Medium, 2 Tall) and Small is 3, so a saved choice keeps its meaning.
- **`ReviewBuddyMenu`** is the Review Buddy menu, built like the Edit menu: it is added to the
  menu bar and removed as the page changes.
- **`StylePopupWindow()`** gives a popup its shadow and rounded corners (Windows 11, macOS), and
  `SetGlassBackground()` draws the blurred background on macOS.
- **`Process::QuoteArgument()`** quotes an argument for a Windows command line. `Process` starts
  programs with `CreateProcessW`.
- **Find in Files:** `FindInFiles` searches with ripgrep (this machine, WSL, ssh) and numbers its
  searches, so a late answer of a replaced search is ignored. `FindInFilesDlg` is the dialog and
  `FindResultsView` the results. `Ripgrep` and `ToolDescription` pin the release and its SHA-256
  checksums for Linux, macOS and Windows (`.zip` assets, read by `ZipArchive`; `.tar.gz` by
  `TarGz`).
- **`RemoteInstaller`** installs one tool of a `ToolDescription` into a folder of an account: it
  queries the machine, checks the installed version, downloads once on this computer, verifies the
  checksum, and uploads the file, without blocking the UI thread. It is GUI-free, works for any
  tool and folder, and has a check-only mode so that the caller can ask the user first.
- **`SideView`** is the content of the Side Bar: a `wxAuiNotebook` with the `GlyphTabArt` of the
  application and four tabs (File Explorer, Git, Activity and Find in Files; `SelectTab()`).
- **`SearchablePage`** is the interface of the pages that have a search (`CanFind()`,
  `ShowFind()`, `FindNext()`, `GetFindWhat()`, `GetSearchTarget()`); `FilePage` and `SessionPage`
  implement it, and `MainNotebook` forwards the Find commands to the page that is showing.
- **`FileEvent`** carries a line, a column and a length (a match to select), and
  `wxEVT_CLOSE_ALL_FILES_STARTED` / `_ENDED` surround Close All Files. `FileTreeCtrl` lists the
  folders that a reveal requires on one joinable thread, from a queue that replaces a folder that
  is already queued.
- **`UiPrefs`** keeps the last Find in Files (pattern, options, globs, folder).
- **tmux, in `src/core/`** (GUI-free): `TmuxCommands` builds the command lines (attach-or-create
  with `new-session -A`, kill, list, has-session, version probe, the session name, and the
  settings adapted to the tmux version); `TmuxClient` runs them on an `Account` and returns
  `Status`/`StatusOr`; `RunShell` runs a shell script on an account (moved out of
  `RemoteInstaller`); `TmuxShim` describes tmux-shim 1.1.1 and its checksums.
  `BuildCommandLine()` has a tmux form, and `SshLoginCommand()` and `ChangeDirCommand()` build the
  login and the `cd` of a remote terminal.
- **`Session`** has `tmuxName`, `remoteHost` and `remoteUser` (saved in `workspace.json`; an older
  file loads as before) and the methods that answer questions about a session:
  `IsTmuxSession()`, `GetTmuxServer()`, `GetTmuxAccount()` and `GetTmuxTarget()`.
- **A session starts in two steps**, called by whoever creates the page: `CreateTerminal()` (the
  terminal and its shell, always) and `StartProgram()` (what runs in it). A tmux session requests
  its machine with `wxEVT_TMUX_MACHINE_PREPARE` and continues when `wxEVT_TMUX_MACHINE` reports
  *ready*; `SessionPage::SendCommands()` holds commands until the program has started.
- **`TmuxMachines`** tracks the machines, prepares each once with a `TmuxPreparer` (probe, check
  for tmux-shim, installation on request), and notifies every page through `EventNotifier`
  (`TmuxMachineEvent`: probing, needs shim, installing, ready, unavailable). Each page has a
  `wxInfoBar` for its questions.
- **`TabBadge`** (in `TabIcon.h`) is the mark on a tab icon (the tmux logo, or a dot); `SessionPage::GetTabBadge()` says which.
  `NewSessionRequest::runInTmux` carries the choice of the Start Agent dialog.
- **Detached** is a `SessionStatus`: `OnTerminated()` queries the machine
  (`TmuxClient::HasSession()`, on a worker thread) to determine whether the tmux session still
  exists.
- **`SshClient::ExecuteStreaming()`** (a libssh channel) passes the output to a callback while the
  command runs; a callback that returns false closes the channel. Find in Files uses it.
- **`MainNotebook::FindOrOpenFile()`** returns the page of a file, or opens it (local, WSL or
  ssh), with the folders to search as an optional argument.
- **`FileBrowserDlg`** and **`EditAgentDlg`** list a remote folder or the programs of a host on a
  worker thread.
- **`AGENTS.md`** describes how agents work with the user, and `CLAUDE.md` defines the rules for
  names (no `XxxOf` / `XxxFor`), for methods on the object instead of helpers, and for anonymous
  namespaces.
- **wxCrafter files** are changed through `UI.wxcp` and `wxcgen`; `CLAUDE.md` describes how.
- The version number is 3.0.0.

## Upgrade Notes

- **The tabs may have a different height than in 2.0.** The default is Medium on Windows and macOS
  and Short on Linux. Medium is 8 DIPs of padding, slightly more than the 6 of 2.0, and on Linux
  Short is 4 DIPs, tighter than the 10 of 2.0. Another height can be selected in **Settings → Tab
  Height** or from the context menu of the tab bar.
- **The Side Bar tabs follow the tab height** of the notebook.
- **New shortcuts:** Ctrl+Shift+A (Archived Sessions), Ctrl+Shift+R (Start Review Buddy),
  Ctrl+Shift+F (Find in Files) and Ctrl+` (Toggle SideBar; Control+` on macOS). Programs in a
  terminal no longer receive these keys while Kennel handles them.
- **The Review Buddy button is not in the toolbar of a terminal.** An agent session still has it,
  as does the new Review Buddy menu.
- **The first search on a machine downloads ripgrep.** This computer needs internet access for
  it, once for each machine that is searched, and Kennel asks for confirmation first. The file is
  written into `~/.kennel/bin` on that machine.
- **Sessions run in tmux by default** (except for a Windows shell on this computer). **tmux 3.3 or
  newer must be installed** on every machine that holds sessions (this computer on Linux and
  macOS, the WSL distributions, and the ssh hosts). A session on a machine without it shows a
  message bar and does not start until tmux is installed (use **Restart**), or the session is
  closed. Sessions that were saved before 3.0 have no tmux name: they are restored as before,
  without tmux. A new session is required to use tmux.
- **Close now stops a session on its machine.** To leave an agent running, use **Archive**, or
  quit Kennel: both leave it running.
- **tmux-shim is downloaded to `~/.kennel/bin` on each machine** (Linux x86_64 and aarch64) after
  **Download** is selected. This computer needs internet access for it, once for each machine.
- **Native Windows sessions are not run in tmux.** Use WSL or ssh for sessions that must outlive
  Kennel.
- **The tmux setting and its commands** (`tmuxEnabled`, `tmuxAgentSettings`,
  `tmuxTerminalSettings`) are edited in **Settings → Tmux** and stored in
  `~/.kennel/.persist.json`.
- **The layout:** the Side Bar is the only pane next to the pages. It has no caption, is on the
  right and a quarter of the window wide at first, and has the File Explorer, Git, Activity and
  Find in Files as tabs. **View → SideBar Position** puts it on the left or on the right. The tabs
  can be moved and split, and Kennel keeps their layout between runs.
  The Activity and Output panes of earlier builds are gone, and a layout that was saved with them
  is not applied.
