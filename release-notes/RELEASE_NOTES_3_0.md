# Kennel 3.0.0 Release Notes

*Covers changes since tag `2.0.0`.*

Kennel 3.0.0 runs **sessions in tmux**. An agent, or a plain terminal, runs in a tmux session on
its machine (a WSL distribution, or a host reached over ssh), so it continues to run when Kennel is
closed or the connection is lost, and the next start attaches to it again. The
status icons of a tab continue to work through tmux with the help of **tmux-shim**, which Kennel
installs on the machine after confirmation.

The release also adds **Find in Files**: a search of a folder of this computer, of a WSL
distribution or of a host reached over ssh, where a match opens in the editor with the text
selected. The results are shown in a new **Output** pane. Kennel installs the search program
(ripgrep) on the target machine after confirmation. The editor gets **language servers** (LSP) with semantic
highlighting for C/C++, Go, Rust, Python and PHP, plus bookmarks, line numbers, Highlight Word and
editing with many carets; a session can be **duplicated with a summary** of its work, and a prompt
can be **broadcast** to all agents. In addition, the release refines the window introduced in 2.0: the main toolbar has a new design, an **Archive** button, and the shortcut of
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
  new **Output View** (F1), a pane at the bottom of the window.
- **Language servers**: clangd, gopls, rust-analyzer, pyright and phpantom_lsp colour the words of
  C/C++, Go, Rust, Python and PHP files, on this computer, in WSL and over ssh.
- **The editor**: bookmarks, line numbers, Highlight Word, many carets, and the type of a file
  without an extension from its first line (a shebang, or `<?php`).
- **A file tree that never blocks the window**: everything it does with files runs on a worker
  thread, and the file explorer follows the file that is showing, in the tree of its machine.
- **Duplicate with Summary**, **Refresh All Sessions** and **Broadcast**.
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

- **Agents and terminals run in tmux** on the machine where they run: a WSL distribution or a host
  reached over ssh. The terminal in Kennel is a view of the tmux session, so the agent continues to
  run when Kennel is closed, the application terminates unexpectedly, or the connection is lost.
  A session on this computer does not use tmux, on any platform (tmux is often not installed on
  macOS, and the session used to fail with `env: tmux: No such file or directory`). tmux is
  enabled by default.
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
- **Identification.** The icon of the tab of a session in tmux has a green strip on its
  left (2 DIPs wide, beside the icon, which keeps its size), and the tab of a **Detached** session
  has a yellow dot. The status bar shows `tmux: <name>` next to the host (the tooltip has the full
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
  The defaults are vi keys, the clipboard (OSC 52)
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
- **Results in the Output View.** A search shows the **Find in Files** tab of the Output View, a
  pane at the bottom of the window (see below). **View → Output View** (F1) shows or hides it. A
  toolbar provides **Stop** and **Clear**. The
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

### The file explorer never blocks the window

- **All the work of the file tree is done on a worker thread.** Each tree has one thread and a
  queue of jobs (connect, list a folder, make a file or a folder, delete, read the first line of
  files), so the window does not wait for a disk, a WSL share or an ssh host: listing, opening a
  folder, Refresh, New Folder, New File and Delete are all asked for, and the tree draws the
  answers when they arrive. A folder that was never listed stays closed when it is opened, shows a
  busy cursor, and opens by itself when its listing arrives. A refreshed folder comes back open,
  with the folders in it that were open. This also holds on a remote host, where the tree is
  connected by a job of the thread (the detached thread is gone).
- **A file that is opened is shown in the file explorer of its machine.** Showing a file page
  selects the tree of the account of the file: the one that shows, when it is of that account;
  else the one whose root folder holds the file; else any of the account. A remote file shows its
  remote tree (and starts its connection if needed), and the file is selected, scrolled into view,
  once the folders on the way are listed. The file is selected only when the file explorer is on
  the screen.
- **The root item shows the full path** of its folder, not only its name.
- **Files without an extension have the icon of their type** (see "The editor"). The first
  lines of such files are read by a job after the tree is built; over ssh they are read with SFTP,
  on a connection of their own.
- **Close All Files is lighter.** The file explorer does not follow every page that is shown while
  the files are closed one after the other.

### Language servers

- **Semantic highlighting from a language server.** A file that is open in the editor is sent to
  the language server of its language, and the words that the server names (functions, classes and
  variables) are coloured by the lexer of the editor. The servers are **clangd** (C and C++),
  **gopls** (Go), **rust-analyzer** (Rust), **pyright** (Python) and **phpantom_lsp** (PHP). Colours
  are the first feature; pyright does not offer semantic tokens, so Python keeps the colours of the
  lexer. A server that does not answer UTF-8 positions is used anyway, with the positions read as
  bytes.
- **A server for each machine and folder.** The server runs on the machine of the file (this
  computer, a WSL distribution, or a host reached over ssh, through `wsl.exe` or `ssh`) in the
  folder of the session that holds the file, and is ended when no session, terminal or file needs
  it. A file that was opened before any session held its folder looks for a server again when a
  session is added.
- **A server that is installed is used.** Kennel first looks for the server on this computer (the
  `PATH`, MSYS2 on Windows, Xcode and Homebrew `llvm` on macOS) and starts what it finds, without a
  question. On this computer that gives clangd its own include folders, so `foo->bar()` is
  coloured.
- **Install after confirmation.** For clangd, rust-analyzer and phpantom_lsp, in WSL and on ssh
  hosts, Kennel asks **Download and Install?** and installs a pinned version, verified with a
  SHA-256 checksum, as for ripgrep (the downloads of clangd are 100 to 118 MB; `.zip`, `.tar.gz` and
  plain `.gz` assets are read). **gopls and pyright are not installed by Kennel**: if they are not
  found, the message says how to install them (`go install golang.org/x/tools/gopls@latest`,
  `npm install -g pyright`). They are looked for on this computer only.
- **Remember my answer.** The question has a checkbox. A **No** that is remembered is stored for
  the machine and the language in `~/.kennel/dialog-answers.json`, and the question is not asked
  again at the next file or the next start. **Settings → Answers** lists the stored answers, with
  **Remove** and **Remove All**.
- **A dead server is started again** after 1 s, 2 s, 4 s ... (at most 60 s); after 5 failures in a
  row it is given up, and a server that ran for 60 s starts the count again. The editors that had
  files open on it are told, and register again with the next server that is ready.
- A file that is reloaded or saved under another name is closed and opened on the server again.
- **Logging:** every language server line in the log starts with `LSP | `. New options:
  `--log-level` (trace, debug, info, warn, error) and `--stdout` (also writes the log to the
  console; on Windows it attaches to the console of the program that started Kennel).

### The editor

- **Bookmarks.** **Edit → Bookmarks**: Toggle (Ctrl+F2), Next (F2), Previous (Shift+F2) and Clear
  All (Ctrl+Shift+F2), also as a group of tools on the file toolbar. A bookmark is a magenta mark
  in a margin of its own. Ctrl is Cmd on macOS (Control+F2 belongs to the system there).
- **Show Line Numbers** (Edit menu and toolbar tool, remembered in the preferences). It applies to
  the file editor, not to the commit message editor.
- **Highlight Word** marks the other places of the selected word (whole words, the case counts)
  in the lines that are shown. It is an option of the Edit menu and the file toolbar, and is off
  while there is more than one selection.
- **Many carets and selections.** Ctrl+click adds a caret, Ctrl+drag a selection, and
  Alt+Shift+arrows a rectangular one; typing, deleting and pasting go to all of them.
- **F3 / Shift+F3 with text selected** looks for that text: the search bar shows it and the match
  is the next (previous) one from the selection.
- **A reload keeps your place.** The bookmarks, the caret and the first visible line are put back
  after a reload of a file, local or remote.
- **File tabs show the file name** (before, `folder/name`). **Settings → Show the folder name in
  file tabs** brings the folder back, and the open tabs follow at once.
- **Files without an extension have a type.** The type of a file whose name says nothing (for
  example `codelite-remote`) comes from its first line: a shebang (`#!/usr/bin/env python3`, also
  `env -S`, and versions such as `python3.11`) for Python, bash, sh, zsh, ksh, dash, ash, Ruby,
  node and PHP, or `<?php`. The editor colours it, the language server serves it, and the tab and
  the file explorer show the icon of the type, for local, WSL and ssh files and in Save As.
- **Go and PHP** have their own lexer colours, and the editor colours the words of a language
  server with the keyword sets of the C++, Rust, Python and PHP lexers.
- The Close tool is no longer on the file toolbar (**Edit → Close Editor** and the tab close the
  file).

### The Output View

- **A pane at the bottom of the window** (**View → Output View**, F1), hidden at first, with a
  caption and a close button. It has two tabs: **Git**, with the terminal of the git view (Push
  and Pull run there; it was too narrow in the Side Bar), and **Find in Files**, with the results of
  a search (moved from the Side Bar). A command or a search shows the pane, and the focus goes back
  to the page when it hides. It follows the tab outline colour, the tab height and the theme.
- **New Terminal asks for the login shell first**, so that a cancel leaves nothing behind.

### Sessions: summary, refresh and broadcast

- **Duplicate with Summary...** in the menu of a session tab. The agent of the session writes a
  summary of its work on its machine, and a new session starts with it: the **Start Agent** dialog
  opens, the summary is written into the folder of the new session (never over a file that is
  there), and the first prompt of the new agent tells it to read the file and delete it. The page
  waits with a **Cancel** (Ctrl+C to the agent) for up to 5 minutes. The item is enabled when the
  agent runs and is not working or waiting for an answer. The file is removed when nobody will read
  it (a failed launch, a page that goes away, a program that ended). The idle monitor (a session
  quiet for 3 s) is removed, because the status that the program reports tells what it does.
- **File → Refresh All Sessions** (Ctrl+Shift+F5) restarts every agent session, not the plain
  terminals and not the jobs.
- **File → Broadcast...** (Ctrl+Shift+B) sends a message to the agent sessions that you check
  (all are checked at first, with **Check All** and **Uncheck All**), typed as one line and
  followed by Enter. A session whose agent does not run, or waits for an answer, does not get it,
  and the dialog says which.
- **Archived Sessions** accepts several sessions: **Restore** and **Delete** work on the
  selection, and Delete asks once. If a restore fails, the dialog stays open and names the sessions
  that failed.
- **Start Agent:** with **Use different working directory for the agent**, the new box **Create a
  folder with the session name in it** starts the agent in `<folder>/<session name>`, and the
  dialog shows the folder the agent starts in as you change the name, the agent, the group or the
  folder.

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
- **Rows:** the file tree has no row lines and a smaller indent; macOS has row lines in the file
  tree and in the git list.
- **The status bar shows the first line of the help** of a menu item or a tool (a tool has its
  shortcut on a second line for the tooltip).
- **The splitters of a session page** are painted with the system button shadow colour (the
  terminal background for the review buddy strip); on macOS they use the default drawing.
- **The job icon** shows ten past ten (the hands both pointed to 12).
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
- **A crash at startup on macOS** (`EXC_BAD_ACCESS` in `MainView::RestoreLayout()`): the panes were
  updated while a focus change was running, and the nested update freed the layout that the first
  one was reading. The update is now scheduled after the focus change.
- **A new local terminal showed its prompt in one column**, because the shell started before the
  page was laid out. It starts once the terminal has its size (wxTerminalEmulator is updated).
- **vim showed stray underlines** in the terminal (libtsm).
- **After ssh ended, the terminal kept the mouse tracking, the hidden cursor and the alternate
  screen of tmux.** The local shell now makes a soft reset and turns bracketed paste off once ssh
  exits (not on Windows, where PowerShell and cmd have no `printf`).
- **Start Agent** set the preview text again on every idle event for a path with an `&`.
- **Reload of a file:** the caret column is cut to the line (no jump to the top of the file), and
  a reload that gave no text drops the saved state; the Highlight Word tool was added again on
  every option change.
- **The status bar drew the two lines of the help of a tool over each other.**
- **Windows installer:** `kennel.exe` failed to start because the installer shipped `zlib1.dll`
  while MSYS2 has `libz-1.dll`. The DLLs that `ntldd` lists for `kennel.exe` are copied, whatever
  their names, and a second build no longer fails to find them.
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
  application and three tabs (File Explorer, Git and Activity; `SelectTab()`). **`OutputView`** is the pane at
  the bottom.
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
- **`TabBadge`** (in `TabIcon.h`) is the mark on a tab icon (a green strip for tmux, or a dot); `SessionPage::GetTabBadge()` says which.
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
- **Language servers** (`src/core/lsp/`): `LspManager` is the one entry point (`GetServerFor()`):
  it checks (a server that is installed, then `RemoteInstaller`), installs after consent, launches,
  and does the handshake, and reports with events. `LspServerKey` is an account and a `LspKind`;
  `LspServer` is the process (`wsl.exe`, `ssh` or the program); `JsonRpcFramer` cuts the messages on
  a thread of its own; `LspClient` holds the logic (`didOpen`, `didChange` from the change counter,
  `didSave`, `didClose`, semantic tokens, and the termination). `LspSemanticTokens` decodes the
  tokens. `IEditor` (implemented by `FilePage`) and `IManager` (implemented by `KennelManager`,
  given to the core) let the core fetch an editor by its ID (`FilePageUID`) from the live
  notebook instead of keeping a copy. `Clangd`, `RustAnalyzer` and `Phpantom` pin their releases.
- **`PageViewEvent` and `FileEvent`** are in `src/core`, and the page events are renamed
  `wxEVT_SESSION_PAGE_*` to `wxEVT_PAGE_*` (ADDED, REMOVED, RENAMED, SHOWN, ACTIVITY, MOVED). They
  carry the working directory, the page type (file, session, terminal), the path of a file and the
  ID of its editor, and are sent for files too.
- **`Status`** has a reason code (`StatusCode`: `kUnavailable` for a failure that may pass,
  `kNotAvailable`, `kPermissionDenied`, `kUnknown`).
- **`DialogAnswers`** keeps the answers to questions that are asked once, in
  `~/.kennel/dialog-answers.json`.
- **`SftpClient`** has `ResolvePath()`, `Exists()`, `RemoveFile()`, `PutFile()` (with an exclusive
  mode, and writes in chunks of 4 MB), `RemoveTree()` and `ReadFirstLines()`.
  `RemoteInstaller` reads plain `.gz` assets and downloads up to 256 MB.
- **`platform::Which()`** (in `core/Helpers`) looks for a program on this computer: the `PATH`
  first, then MSYS2 on Windows, and the folders of Xcode and Homebrew `llvm`.
- **The file tree:** `FileTreeCtrl` has one joinable thread and a queue of jobs
  (`Connect`, `ListRoot`, `ListDir`, `ReadFirstLines`, `MakeFolder`, `MakeFile`, `Remove`); the
  answers come on the `EventNotifier` with the UID of the tree (`FileTreeConnectedEvent`,
  `FileTreeChangedEvent`, `FileTypesEvent`; the last two are in `src/core`), and `SideView` finds
  the tree by its UID (`FindTreeByUID()`). `FileProvider::ReadFirstLines()` is implemented by the
  local, the WSL and the remote providers. `Shebang` parses the first line of a file;
  `Bitmaps::GetFileIcon()` takes it as an optional argument.
- **`TerminalManager`** makes the terminals of `SessionPage` (the agent's and the review buddy's):
  shell command, environment, theme and preferences. `GitTerminals` owns the terminals of the git
  panels; the panels talk to `OutputView` with events (`wxEVT_GITVIEW_RUN_IN_TERMINAL`, `_SHOWN`
  and `_CLOSED`).
- **`SessionHandoff`** (the summary), `HandoffDelivery` (the file for the new session) and
  `PromptFileRemover` (deletes only files; `KennelApp::OnExit()` waits up to 5 s for it).
  `BroadcastDlg` is designed in `UI.wxcp`.
- **`EditorState`** (RAII) keeps the bookmarks, the caret and the first visible line over a reload.
- **Build:** `build.sh` installs the MSYS2 packages once (a marker file holds the list;
  `KENNEL_SKIP_PACMAN=1` skips it), assets are copied by a `kennel_assets` target on every build,
  `scripts/CopyRuntimeDlls.cmake` copies the DLLs of MSYS2 that `ntldd` lists, and CMake copies
  whatever `libcrypto-*.dll` MSYS2 ships.
- **`CLAUDE.md`** has hard rules at the top for code: all tree work is asynchronous, no detached
  threads, functions under 20 lines, no global functions, no templates, and finding an object by ID
  instead of keeping state. It also has the rule to do exactly what the user says and to ask.
- The version number is 3.0.0.

## Upgrade Notes

- **The tabs may have a different height than in 2.0.** The default is Medium on Windows and macOS
  and Short on Linux. Medium is 8 DIPs of padding, slightly more than the 6 of 2.0, and on Linux
  Short is 4 DIPs, tighter than the 10 of 2.0. Another height can be selected in **Settings → Tab
  Height** or from the context menu of the tab bar.
- **The Side Bar tabs follow the tab height** of the notebook.
- **New shortcuts:** Ctrl+Shift+A (Archived Sessions), Ctrl+Shift+R (Start Review Buddy),
  Ctrl+Shift+F (Find in Files), Ctrl+Shift+F5 (Refresh All Sessions), Ctrl+Shift+B (Broadcast),
  F1 (Output View), and the editor keys Ctrl+F2, F2, Shift+F2 and Ctrl+Shift+F2 (bookmarks).
  Programs in a terminal no longer receive these keys while Kennel handles them.
- **Toggle All Views (Ctrl+M) is now Toggle SideBar** in the View menu, with the same key.
  **Session Terminal** (the terminal at the bottom of a session) is Ctrl+` (Ctrl+§ on macOS).
  **Rename Session** (F2) is disabled while a file is showing, because F2 is Next Bookmark there.
- **The Review Buddy button is not in the toolbar of a terminal.** An agent session still has it,
  as does the new Review Buddy menu.
- **The first search on a machine downloads ripgrep.** This computer needs internet access for
  it, once for each machine that is searched, and Kennel asks for confirmation first. The file is
  written into `~/.kennel/bin` on that machine.
- **Sessions run in tmux by default** (except the sessions on this computer). **tmux 3.3 or newer
  must be installed** on every machine that holds sessions (the WSL distributions and the ssh
  hosts). A session on a machine without it shows a
  message bar and does not start until tmux is installed (use **Restart**), or the session is
  closed. Sessions that were saved before 3.0 have no tmux name: they are restored as before,
  without tmux. A new session is required to use tmux.
- **Close now stops a session on its machine.** To leave an agent running, use **Archive**, or
  quit Kennel: both leave it running.
- **tmux-shim is downloaded to `~/.kennel/bin` on each machine** (Linux x86_64 and aarch64) after
  **Download** is selected. This computer needs internet access for it, once for each machine.
- **Sessions on this computer are not run in tmux** (on Windows, Linux and macOS). Use WSL or ssh
  for sessions that must outlive Kennel.
- **The tmux setting and its commands** (`tmuxEnabled`, `tmuxAgentSettings`,
  `tmuxTerminalSettings`) are edited in **Settings → Tmux** and stored in
  `~/.kennel/.persist.json`.
- **The layout:** the Side Bar is the pane next to the pages. It has no caption, is on the right
  and a quarter of the window wide at first, and has the File Explorer, Git and Activity as tabs.
  **View → SideBar Position** puts it on the left or on the right. The tabs can be moved and
  split, and Kennel keeps their layout between runs. The **Output View** (F1) is a second pane,
  at the bottom of the window and hidden at first (also after **Reset Layout**); it has the Git
  and Find in Files tabs. The Activity and Output panes of earlier builds are gone, and a layout
  that was saved with them is not applied.
- **The tabs of the notebook have a fixed width** (160 DIPs on a horizontal bar, narrower when
  they do not fit), and the tab of a file shows the file name only (see "The editor").
- **The status bar no longer shows `Kennel <version>`**; the version is in the About dialog.
