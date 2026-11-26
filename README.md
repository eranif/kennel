<div align="center">

# 🐾 Kennel

**A workspace for AI CLI agents, terminals and files**

[![Kennel in action](media/promo/teaser.gif)](media/promo/kennel-promo.mp4)

| Platform | Download | Notes |
|---|---|---|
| **macOS (Apple Silicon)** | **[kennel-macOS_26.5.2_arm64.zip](https://github.com/eranif/kennel/releases/download/2.0.0/kennel-macOS_26.5.2_arm64.zip)** | A signed and notarized application bundle. |
| **Windows 11** | **[kennel-2.0.0-amd64-installer.exe](https://github.com/eranif/kennel/releases/download/2.0.0/kennel-2.0.0-amd64-installer.exe)** | An installer, built on GitHub Actions. |
| **Linux** | [Build from source](BUILDING.md) | Run `./build.sh`. |

**[▶ Watch the one-minute tour](media/promo/kennel-promo.mp4)** ·
**[Open the interactive tour](https://htmlpreview.github.io/?https://github.com/eranif/kennel/blob/main/media/promo/kennel-promo.html)**

![Version](https://img.shields.io/badge/version-3.0.0-blue)
![License](https://img.shields.io/badge/license-BSD--3--Clause-green)
![Platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Linux%20%7C%20Windows-informational)

</div>

Kennel is a fast, native desktop application for working with interactive AI CLI agents: **Claude
Code**, **OpenAI Codex**, **Kiro CLI**, **OpenCode** or any tool of your own. It runs many agent
sessions side by side in one window, and it gives you what you need around them: a file explorer,
a git view, an editor, a code reviewer that is a second agent, **Find in Files** on any machine,
and a search for everything in the application.

Sessions run on this machine, in **WSL** or on **remote hosts over SSH**, and the tools around
them always show the session you are working in. **When you restart the application or reboot the
machine, every session returns where it left off**, using the native resume flag of the agent
(for example Claude's `--continue` or Kiro's `--resume`). On Linux, macOS, WSL and remote hosts,
sessions run in **tmux**, so **an agent continues to run when Kennel is closed**, and Kennel
attaches to it again at the next start (see [Sessions in tmux](#sessions-in-tmux)).

---

## Download

The downloads are in the table at the top of this page, and all the files are on the
**[Releases](https://github.com/eranif/kennel/releases)** page. On Linux, build Kennel from source,
see [BUILDING.md](BUILDING.md).

---

## Why Kennel?

Anyone who uses AI coding agents from the terminal knows the problems: a dozen terminal tabs, no
way to tell which agent is waiting for you, and lost context whenever a window closes. Kennel
solves these problems and adds the tools that surround an agent.

- 🗂️ **One window for everything.** Agent sessions, plain terminals and files are the tabs of one
  notebook. The tabs of a session group sit together under the name of the group, and
  **Ctrl+Tab** switches between pages in most-recently-used order.
- 🚦 **See what each agent is doing.** A spinner, a check mark or an orange pulse on the tab tells
  you whether an agent is working, done or blocked and waiting for you. The **Activity** tab of
  the Side Bar lists these sessions in one place.
- 🔄 **Never lose your place.** Sessions, terminals and open files return at the next start, and
  agents resume their conversation with their native resume flag. A session can also be
  **archived** and restored later.
- 📁 **Files and git next to the session.** The **Side Bar** has a file explorer and a git view
  that always show the active session, on this machine, in WSL or on a remote host.
- 🤖 **A second agent that reviews the first.** **Review Buddy** reviews the changes of a session
  and sends its findings back to the first agent, with no one in between.
- 🧵 **Agents that outlive the window.** Sessions run in **tmux** on this machine (Linux and
  macOS), in WSL and on remote hosts. When Kennel is closed or the connection is lost, the agent
  continues to run, and the next start attaches to it again.
- 🔎 **Find anything fast.** **Goto Anything** (`Ctrl`/`Cmd`+`Shift`+`P`) searches the menus, the
  open pages and the actions of the current page. **Find in Files** (`Ctrl`/`Cmd`+`Shift`+`F`)
  searches a folder of this machine, of WSL or of an SSH host, and a click on a match opens the
  file at that line.
- ⚡ **A true, fast terminal.** Every session is a full PTY terminal, drawn with **OpenGL** on all
  platforms. It supports advanced ANSI sequences such as **OSC 9;4** (progress bars) and
  **OSC 7501** (Program Status Protocol), the clipboard, and mouse events for TUI applications,
  so any interactive CLI works as it does in your shell. See [The Terminal](#the-terminal).
- 🧩 **Any agent, no code needed.** The first start finds the agents that are installed. More
  agents are defined in a wizard or in JSON, locally or over SSH.
- 🌈 **Make it yours.** Terminal color themes (the default is **Mariana**), an application theme
  that can follow the terminal theme, your own fonts, and an icon for each agent.

---

## Screenshots

*Real screenshots of Kennel 2.0 with demo sessions.*

| One window for every agent | Working, done and blocked at a glance |
|---|---|
| ![Sessions in one window, grouped by session group](media/screenshots/api-refactor.jpg) | ![A spinner, a check mark and a pulse on the tabs and in the Activity tab](media/screenshots/infra.jpg) |

| Ctrl+Tab switches in most-recently-used order | Goto Anything |
|---|---|
| ![The Ctrl+Tab session switcher](media/screenshots/switcher.jpg) | ![Goto Anything](media/screenshots/goto-anything.jpg) |

| The File Explorer follows the active session... | ...and shows the folder of another session |
|---|---|
| ![File Explorer of a session](media/screenshots/explorer-api.jpg) | ![File Explorer of another session](media/screenshots/explorer-docs.jpg) |

| Review Buddy: a second agent reviews the first | First start: Kennel finds your agents |
|---|---|
| ![Review Buddy next to its session](media/screenshots/review-buddy.jpg) | ![The agents that the first scan found, in the toolbar](media/screenshots/first-scan.jpg) |

| Start an agent | Define a new agent |
|---|---|
| ![The Start Agent dialog](media/screenshots/start-agent.jpg) | ![The New Agent Wizard](media/screenshots/new-agent-wizard.jpg) |

The screenshots and the tour are made with the scripts in [`media/`](media/AGENTS.md).

---

## What's New in 3.0

- **Sessions in tmux.** Agents and plain terminals run in tmux on Linux, macOS, WSL and remote
  hosts, and continue to run when Kennel is closed. **Close** stops a session, **Archive** keeps
  it running, and a tab whose terminal has ended while its tmux session is still running is shown
  as **Detached** and can be attached again with **Restart**. Problems with tmux are reported in
  a message bar on the tab, not in a dialog. See [Sessions in tmux](#sessions-in-tmux).
- **Find in Files** (`Ctrl`/`Cmd`+`Shift`+`F`) for this machine, WSL and SSH hosts, with the
  matches in the new **Output View** (`F1`). A remote search streams its matches while it
  runs. ripgrep is installed on the target machine after confirmation.
- **A new main toolbar** with grouped buttons, an **Archive** button, and the shortcut of each
  button in its tooltip.
- **Tab Height**: Short, Small, Medium or Tall, applied live to the notebook and the Side Bar.
- **A Review Buddy menu** and the shortcut `Ctrl`/`Cmd`+`Shift`+`R`.
- **Goto Anything and the page switcher open at the top** of the window, with rounded corners.
- **Remote hosts do not block the window**: the folder browser and the agent dialog query a host
  in the background, and **Edit Agents** lists the programs installed on the host.
- A file that is already open is shown again when it is selected in the results or linked from a
  terminal, including remote files.

See the [Release Notes](release-notes/RELEASE_NOTES_3_0.md) for the complete list of changes and
the upgrade notes. Earlier releases: [2.0.0](release-notes/RELEASE_NOTES_2_0.md),
[1.4.0](release-notes/RELEASE_NOTES_1_4.md), [1.3.0](release-notes/RELEASE_NOTES_1.3.0.md).

### What was new in 2.0

Version 2.0.0 was a redesign of the main window and of the terminal page:

- **One notebook** for all sessions, terminals and files, in place of the session tree. Tabs are
  grouped by session group, and the position of the tabs is yours to choose.
- **A Side Bar** on the right, with the **File Explorer**, **Git** (status, diff, add, commit, log,
  blame, push and pull, for local, WSL and remote locations), **Activity** (the working, done and
  blocked sessions) and **Find in Files** (the results of a search) as tabs.
- **Archive** a session instead of closing it, and restore it from **File → Archived Sessions**.
- **Goto Anything** for menus, pages and page actions.
- **A toolbar, a header and a progress bar** for every terminal and file, on the left of the page,
  with the progress of Review Buddy shown in the header.
- **A search bar** for files and terminals, with replace for files.
- **Open files and terminals** return with the workspace, and file links in the terminal follow
  `cd` and open at the right line and column.
- **An application theme** that follows the terminal theme, or the system, dark or light setting.
- **Agent discovery** on the first start for **Claude Code**, **Codex**, **Kiro CLI** and
  **OpenCode**, including WSL distributions and SSH hosts.
- **Windows**: a newer ConPTY, color emoji, and an installer built on GitHub Actions.

---

## Core Concepts

| Concept | Description |
|---|---|
| **Agent** | A definition of *how to launch* an AI CLI: its executable, arguments, environment, icon and, optionally, a remote host or a WSL distribution. Kennel finds and defines **Claude Code**, **OpenAI Codex**, **Kiro CLI** and **OpenCode** when they are installed. |
| **Session** | A named, running instance of an agent in a working directory. Each session is a live terminal, and it runs in a **tmux** session on its machine (not on Windows), so it outlives the window. |
| **Group** | A set of related sessions. The tabs of a group are shown together under a header with the name of the group. The **Default** group always exists. |
| **Terminal** | A plain terminal without an agent. Kennel restores it with its folder and shell. |
| **File** | A file open in the built-in editor, local or remote. |

The main area is a notebook with one tab for each session, terminal and file. The **Side Bar**
shows the files and the git status of the active page, and its **Activity** tab lists the
sessions that are working, done or blocked.

---

## Getting Started

### 1. First start

The first time Kennel starts, it looks for `claude`, `codex`, `kiro-cli` and `opencode` on this
machine, in the background, and defines an agent for each tool it finds. The status bar shows
what it found, and each new agent gets a button in the toolbar. Kennel then asks once whether to
search your **WSL distributions** (Windows only) and the **hosts of your SSH config** as well.
Agents can also be defined by hand at any time (see [Managing Agents](#managing-agents)).

### 2. Start an agent

Press **`Ctrl`/`Cmd`+`T`**, choose **File → Start Agent...**, or click the agent button in the
toolbar on the left. The **Start Agent** dialog provides:

- **Agent**: which CLI to launch.
- **Session Name**: a unique, descriptive label for the session.
- **Session Group**: an existing group, or a new name to create a group.
- **Working Directory**: where the agent runs. You can browse this machine, a WSL distribution
  or a **remote host over SSH**. Kennel remembers the last 10 folders.
  - *Resume the Latest Session in This Folder* relaunches the most recent agent run from that
    folder instead of starting a new one.

### 3. Work with your session

Click a tab to bring its terminal to the front. On the left of every session there is a vertical
**toolbar**:

| Tool | Description |
|---|---|
| **Restart** | Restarts the session (also **File → Refresh Current Session**, `F5`). A session in tmux attaches to its tmux session again, and asks its machine about tmux again. |
| **Review Buddy** | Starts a second agent that reviews the work of the session, and controls the review. |
| **Terminal** | Opens a terminal named `<Session> \| Terminal` on the same host and in the same folder as the session (agent sessions only). On a remote host it logs in with ssh and runs in tmux there. |
| **Copy**, **Paste** | Clipboard actions for the terminal that has the focus. |
| **Find** | Opens the search bar of the terminal. |

A **header** below the terminal shows `Agent Name | Title | Working Directory`, and a thin
**progress bar** shows the progress that a program reports. The tab of an agent shows what it is
doing:

| Icon | Meaning |
|---|---|
| A turning ring | The agent is **working**. |
| A green check mark | The agent is **done**. The mark goes away when you look at the page. |
| An orange pulse | The agent is **blocked** and needs you. |

The same icons are in the **Activity** tab of the Side Bar, which lists the blocked sessions first. Kennel also
asks for your attention when a background session finishes while the window is inactive.

### 4. Organize your sessions

Right-click a tab for its context menu:

- **Rename...** renames the session (or **Rename Terminal** for a plain terminal).
- **Duplicate...** opens **Start Agent** with the agent, the group and a unique name filled in.
- **Move To Group** sends the session to another group, or creates a group with **New Group...**.
  A group that loses its last session is removed, except for **Default**.
- **Archive** stores the session so that you can restore it later (see
  [Archive a Session](#archive-a-session)). Its tmux session keeps running.
- **Close** closes the session. A session in tmux is **stopped**: its tmux session ends with it.

The **Group** submenu works on the whole group: **Start Agent...** in the group,
**Rename Group...**, **Close Group** and **Refresh Group** (restarts every session of the group).

Right-click the empty part of the tab bar to change the **Tab Position** (top, bottom, left or
right), or to turn **Group Tabs by Session Group** on or off.

---

## The Side Bar

The Side Bar is on the right, a quarter of the window wide at first, and has three tabs: **File
Explorer**, **Git** and **Activity**. Drag a tab to change the order, or drag it
to an edge of the Side Bar to split the Side Bar in two. Kennel keeps the order and the split
between runs. The File Explorer and Git tabs show the
location of the page you are working in: a folder on this machine, in a WSL distribution or on a
remote host, and they **change when you change page**. The Activity tab lists the sessions that
are working, done or blocked. A **terminal** has its own views, which follow the folder its shell is in.

The **Output View** is a pane at the bottom of the window, as wide as all of it (`F1`, or
**View → Output View**). It has two tabs: **Git**, the terminal where **Push** and **Pull** of the
Git tab run (the one of the location that the Side Bar shows), and **Find in Files**, the results
of a search. Its close button hides it, and Kennel keeps its height; a git command or a search
shows it again.

### File Explorer

- Double-click a file to open it in the editor. Typing selects the next matching entry.
- The context menu provides **Refresh**, **New Folder...**, **New File...**, **Delete...**,
  **Show in Finder / Explorer / File Manager**, **Open with Default App**, **Copy Path** and
  **Set This Folder as Root**.
- A remote tree connects in the background, so a slow host never blocks the window.

### Git

The Git tab shows the branch and the changed files of the repository, with a status and a file
type icon for each file. A double-click opens the **diff** in a read-only page.

| Action | Description |
|---|---|
| **Add** | Stages the selected files. |
| **Reset** / **Reset Repo** | Restores the selected files, or the whole repository, after confirmation. |
| **Commit** | Opens a message dialog with **Amend** and **Signoff** options. |
| **Log** / **Blame** | Shows the log of the repository, or the blame of the selected file, in an editor. |
| **Push** / **Pull** | Runs in a terminal in the **Git** tab of the Output View, so git can ask for credentials. |
| **Folder** | Shows the git status of another folder. |

By default, the Side Bar shows the working folder of the session. To use another folder,
right-click it in the explorer and choose **Set This Folder as Root**, or click **Folder** in the
git toolbar and browse to it (a parent folder is allowed). Only the active session changes, and
Kennel saves the choice with the session.

---

## Archive a Session

Archiving removes a session from the notebook and keeps it in `workspace.json`, so that you can
restore it later. Terminals cannot be archived. The **tmux session** of an archived session keeps
running on its machine, so a restore attaches to the agent that was working all along; **Delete**
stops it.

- Use **Archive** in the context menu of a tab. If the agent is working, Kennel asks first.
- When you close an agent session, Kennel asks whether to **Archive**, **Close** or **Cancel**.
  Select **Do not ask again** to remember the answer, and change it later under
  **View → Close Button of a Session**.
- **File → Archived Sessions...** lists the archived sessions, with a filter box. Sort by any
  column, **Restore** a session with its resume option (the dialog stays open, so you can restore
  several sessions), or **Delete** an entry for good. The **tmux** column says what each archived
  session's tmux session is doing: *Running* (a restore attaches to it), *Ended* (a restore starts
  a new one with the resume option), or *Unknown* (its machine could not be asked).
- The **Archive** button of the main toolbar (`Ctrl`/`Cmd`+`Shift`+`A`) opens the same list.

---

## Goto Anything

Press **`Ctrl`/`Cmd`+`Shift`+`P`** (it also works inside a terminal), or choose
**Search → Goto Anything...**. It opens at the top of the window. Type words to filter the list, move with the arrow keys or Page
Up / Page Down, press Enter to select and Escape to close. The list contains:

- the actions of the current page, such as **Restart Session**, **Start Review Buddy with ...**
  and **Close Review Buddy**;
- the open sessions, terminals and files;
- every enabled item of the menu bar, with its keyboard shortcut.

---

## The Terminal

The terminal is the core of Kennel. It is a true terminal emulator, not a text box that imitates
one, and it is built for speed and compatibility.

- **Fast rendering on all platforms.** The terminal is drawn with OpenGL on macOS, Windows and
  Linux, so the output of busy agents and full-screen TUI applications stays responsive. On
  Windows, emoji are drawn in color (with Direct2D), and wide characters (CJK and emoji) are not
  clipped.
- **Advanced ANSI escape sequences.** Kennel implements the sequences that modern CLI tools rely
  on, including:
  - **OSC 9;4** (taskbar progress bars): a thin progress bar above the terminal shows the
    progress that a program reports, as a percentage or as an animation.
  - **OSC 7501** (Program Status Protocol): a program tells Kennel that it is working, done or
    blocked. Kennel shows the status on the tab and in the **Activity** pane.
  - **OSC 7** and **OSC 9;9**: the shell reports its current folder. The header shows it, and
    relative paths that you click in the output are resolved against it.
  - Color queries such as **OSC 11**, so that applications can detect a light or a dark
    background (on Windows, with the newer ConPTY that Kennel ships).
- **Clipboard integration.** **Copy** copies the selected text. When nothing is selected, it
  copies the text that the program in the terminal put on the clipboard itself (for example tmux,
  or a TUI such as Claude Code), in full. **Paste** pastes into the terminal that has the focus.
- **Mouse events for TUI applications.** Mouse tracking goes to the program, so tmux, `mc`, `htop`
  and other TUI applications work with the mouse.
- **File links.** Ctrl+Click a path in the output. Paths such as `src/main.cpp:100:50` open the
  file in the editor at the given line and column, locally, in WSL and over SSH.
- **Everywhere.** The same terminal runs your local shell, a WSL distribution and an SSH session.

---

## Sessions in tmux

Agents and plain terminals run inside **tmux** on the machine where they run: this machine
(Linux and macOS), a WSL distribution, or a remote host over SSH. The terminal in Kennel is a view
of the tmux session. An agent therefore continues to run when Kennel is closed, the connection is
lost or the application terminates unexpectedly. Native Windows sessions (a Windows shell on this
machine) do not use tmux.

**Behavior**

- **Restore.** At the next start, Kennel attaches to the tmux session of each saved session. If a
  tmux session no longer exists (for example after the machine was rebooted), Kennel starts a new
  one with the resume option of the agent.
- **Close and Archive.** Closing a session, a group or all sessions ends their tmux sessions.
  **Archive** leaves the tmux session running, and **Restore** attaches to it.
- **Detached.** When the terminal of a session ends while its tmux session is still running (the
  user pressed `Ctrl+b` `d` and ran `exit`, or the connection was lost), the tab remains and shows
  **Detached**. **Restart** (`F5`) attaches again. If the tmux session no longer exists, the tab
  is closed.
- **Activity status.** **tmux-shim** forwards the working, done and blocked status of the program
  through tmux, so that the spinner, the check mark and the pulse on the tab behave as they do
  without tmux (see below).
- **Terminals.** A plain terminal runs its shell in tmux. A terminal on a remote host logs in with
  ssh again when it is restored, and attaches to its tmux session on that host.
- **Starting a session.** The **Start Agent** dialog has a **Run in tmux** box, which is shown for
  an agent that runs where tmux is available and whose default is set in **Settings → Tmux**.
- **Identification.** The icon of the tab of a session in tmux shows the tmux logo in its lower
  right corner, and the tab of a **Detached** session has a yellow dot. The status bar shows
  `tmux: <name>` next to the host.
- **Tab menu.** The context menu of a tab in tmux has **Kill tmux session and close**, which ends
  the tmux session and closes the tab, without the question about archiving.
- **Configuration.** The tmux session has no status line and follows the size of the Kennel
  terminal. The mouse wheel scrolls the history (`mouse on`). The tmux commands are set in
  **Settings → Tmux**.

**Requirements.** tmux **3.3 or newer** must be installed on the machine, using its package
manager (for example `apt install tmux` or `brew install tmux`). Kennel does not install tmux.
When a machine is used for the first time, Kennel verifies the tmux version and whether
**tmux-shim** is installed. Each machine is checked once, regardless of the number of sessions it
holds, and all machines are checked concurrently on background threads. The window is not blocked:
every session has its terminal immediately, and its program starts when its machine is ready.

**Messages.** Questions and problems are shown in a message bar at the top of the session, not in
a dialog, so that each session can present its own:

| Message | Action |
|---|---|
| *tmux-shim is not installed on X* | **Download** installs it into `~/.kennel/bin` on that machine, once for all sessions of the machine, with a SHA-256 verification. Alternatively, **Close session**. |
| *tmux is not installed on X*, or *older than 3.3* | Install or update tmux on that machine, then use **Restart** (`F5`). Alternatively, **Close session**. |
| *Could not reach X* | Verify the connection, then use **Restart**. Alternatively, **Close session**. |

**Close session** closes the tab and ends its tmux session. No tmux session is stopped without an
explicit action: if the shell of a session ends before its machine is ready, the tab remains
**Detached**.

**Names and sockets.** The tmux session of a Kennel session is named `kennel-<group>-<session>`
(spaces, dots and colons are replaced with `-`), and the name is retained when the session is
renamed. Agents use the tmux server `-L kennel` and terminals use `-L kennel-term`. Both are
started without a configuration file, so a personal tmux configuration is not affected. The sessions
can be inspected with `tmux -L kennel ls` and `tmux -L kennel attach -t <name>`.

**tmux-shim.** [tmux-shim](https://github.com/eranif/tmux-shim) (version 1.1.1, for Linux x86_64
and aarch64) wraps the progress and status sequences of a program in the passthrough of tmux. On a
machine that it does not support (macOS, for example), the session runs in tmux without it and
shows no activity status.

---

## Review Buddy

Review Buddy is an **autonomous code reviewer**. It starts a second agent on the same host as the
session, and the two agents work together until the review is done. **No user intervention is
needed**: you do not copy findings, switch between windows or start another round.

1. Start Review Buddy from the **Review Buddy** menu of the menu bar (it is there while an agent
   session is showing), from the **Review Buddy** drop-down of the session toolbar, from Goto
   Anything, or with **`Ctrl`/`Cmd`+`Shift`+`R`**. Choose the agent that acts as the reviewer from
   the list (when only one agent can review, it starts at once). If a review cannot start (the
   folder is not a git repository, or no agent runs on the same host), a message states the reason.
2. The reviewer reviews the unpushed work of the session in its own pane, next to the session,
   and writes its comments to a file in the working directory (`.agents/reviews/<id>/`).
3. Kennel tells the first agent to read the comments and fix them. The first agent confirms when
   it is done.
4. The reviewer reviews again, and the loop repeats until the reviewer reports that the work is
   clean, the review stalls or fails, or the round limit (5 by default) is reached.

The status line shows the progress at every step (for example `Waiting for review (round 1/5)`),
and the header shows it too. When the review ends, a marker shows the result: 👍 the review is
clean, 👎 it stalled or failed, and 🔴 the round limit was reached with open findings.

You stay in control. At any time you can send the request again, stop the review, open the latest
review, or close Review Buddy, all from the **Review Buddy** menu. A review needs a working
directory that is a git repository.

---

## Files and Search

Kennel contains a built-in editor with syntax highlighting and line numbers.

- **Open files.** Ctrl+Click a path in a terminal to open the file. Relative paths follow `cd`
  (local, WSL and SSH), and `file:line:column` references open at the right position, for local
  and remote files. Remote files are downloaded over SFTP and uploaded again when you save. Use
  **File → New File** (`Ctrl`/`Cmd`+`N`) or **File → Open File...** (`Ctrl`/`Cmd`+`Shift`+`O`) to
  open files directly.
- **Edit menu.** While a file is in view, an **Edit** menu is added to the menu bar. A terminal
  keeps its own key bindings.
- **Search bar.** `Ctrl`/`Cmd`+`F` opens a search bar below the editor or the terminal, with
  incremental search, **Full word**, **Case sensitive** and, for files, **Replace** and
  **Replace all**. `F3` and `Shift`+`F3` find the next and the previous match. The search box
  starts with the text selected in the editor or in the terminal (the first non-empty line, when
  several lines are selected). In a terminal, text selected by a full-screen program takes
  precedence.
- **Find in Files.** See below.
- **Go to Line.** `Ctrl`/`Cmd`+`G` jumps to a line (`:12` or `:12:5`).
- **Unsaved changes.** When Kennel closes, every file with unsaved changes asks to save, discard
  or cancel. Open files return at the next start.
- **A file is opened once.** A click on a match, or on a path in a terminal, shows the page of the
  file when it is open already, local, in WSL or on a remote host (a remote file is not
  downloaded again). **File → Close All Files** closes them all.

### Find in Files

**Search → Find in Files...** (`Ctrl`/`Cmd`+`Shift`+`F`, also while a terminal has the focus)
searches a folder of **this machine, of a WSL distribution or of a host that is reached over SSH**
(Linux or macOS), with [ripgrep](https://github.com/BurntSushi/ripgrep).

- **The dialog** asks what to find, in which folder, and with which options: **Regex**,
  **Match case**, **Whole word**, and the files to **Include** and **Exclude** (globs separated by
  commas, for example `*.cpp, *.hpp`). `.gitignore` and hidden files are handled by ripgrep. It
  starts with the text that is selected in the page that is showing, and with the **closest
  workspace** as the folder: the working folder of a session (or the folder of its file explorer)
  that holds the file, on the same machine. The last choices are remembered.
- **The results** are in the **Output View** (`F1`; **View → Output View**), on a
  **Find in Files** tab: the first line says what was searched and where, the
  matches are grouped by file with the line number and the matched text in color, and the last
  line counts the matches (a search stops at 5000). **Stop** ends a search, **Clear** empties the
  pane. A new search replaces the results. A remote search streams its matches while it runs, and
  **Stop** closes the SSH channel.
- **A click opens the match.** One click (or `Enter`) opens the file at that line, with the text
  selected and the line in the middle of the editor, for local, WSL and remote files.
- **ripgrep installation.** Kennel installs a pinned version (15.2.0) into `~/.kennel/bin` on the
  machine that is searched, and does not use any other `rg` found on that machine. The user is
  asked for confirmation first: nothing is downloaded or written before it is given. The download
  is verified against a SHA-256 checksum that is part of Kennel. A remote machine does not need
  internet access: this computer downloads the file and uploads it.

---

## Scheduled Jobs

Beyond interactive sessions, Kennel can run jobs on a timer, whether or not you are watching:

- A **job** runs either a raw shell command, or sends a one-shot prompt to a configured agent
  that runs in non-interactive mode.
- Each job uses one of two schedules, chosen with the **Schedule mode** dropdown:
  - **Every N Hours**: a fixed interval from 1 to 720 hours.
  - **Daily At**: once a day at a time of the day, set with the time picker on a 24-hour clock
    (for example, every day at 10:00).
- The scheduler checks once a minute, so a job starts within a minute of its target time. A daily
  job is always anchored to the exact `HH:MM` you picked, so a late start never moves the time
  forward from one day to the next.
- Editing the schedule of a job takes effect at once: the next run is computed again when you
  save.
- **Jobs → Manage Jobs...** opens a dialog with **New...**, **Edit...**, **Delete** and
  **Run Now**. The list shows the schedule of each job as `every Nh` or `daily at HH:MM`. A job
  can be enabled or disabled on its own. A disabled job is skipped, but its next-run time keeps
  moving, so no backlog of missed runs fires when you enable it again.
- Schedules are tracked while Kennel is running. If the application is closed (or the machine is
  asleep) at the time of a daily job, that day's run is skipped and not caught up later.
- Jobs start in a dedicated **Jobs** session group. These are one-shot runs, and they are not
  saved in the workspace like regular sessions.
- **Run Now** saves pending edits and focuses the terminal of the new session. Jobs that the
  scheduler starts run quietly in the background and do not take the focus.
- **Jobs → View Job Log...** shows every job run (start, end and failure) that is recorded in
  `~/.kennel/logs/jobs.log`, newest first, with a live filter box. Double-click (or press Enter
  on) a row to see the full record as formatted JSON in a read-only viewer.

---

## Managing Agents

Besides the agents that Kennel finds, you can define your own: a locally installed CLI, an
internal tool, or an agent that runs in WSL or on a **remote machine over SSH**.

Choose **File → Configure New Agent...** to open the **New Agent Wizard**, which has three steps.
**Settings → Manage Agents...** edits or removes the agents that are already defined.

![The New Agent Wizard](media/screenshots/new-agent-wizard.jpg)

### Step 1: Local or Remote?

Choose whether the agent runs on this machine or on a remote host over SSH. For a remote agent,
enter the host address and, optionally, a user name. The **...** button lists your saved SSH
hosts.

### Step 2: What to launch

Configure the identity and the command of the agent:

| Field | Purpose |
|---|---|
| **Name** | Display name shown in menus and tabs. |
| **Executable** | The command to run. The wizard finds the known CLIs (`claude`, `codex`, `kiro-cli`, `opencode`) on this machine or on the remote host. |
| **Launch Args** | Arguments passed on every launch (for example `--agent my-agent`). |
| **Resume Args** | Flags used to resume a previous session (for example `--resume` or `--continue`). Suggestions are available with **...**. |
| **Image** | An SVG icon for the agent. You can browse the shipped icons with **...**. |

### Step 3: Shell & Environment

| Field | Purpose |
|---|---|
| **Login Shell** | Overrides the shell that starts the agent (for example `/bin/zsh` or `wsl.exe`). |
| **Environment Variables** | Extra name/value pairs for the agent process. Use the **New** and **Delete** buttons. |

Click **Finish**, and the new agent is available at once in the toolbar and in the **Start Agent**
dialog.

---

## Keyboard Shortcuts

`Ctrl` is `Cmd` on macOS, except where noted.

| Action | Shortcut |
|---|---|
| Start Agent | `Ctrl` + `T` |
| New Terminal | `Ctrl` + `E` |
| New File | `Ctrl` + `N` |
| Open File | `Ctrl` + `Shift` + `O` |
| Find in Files | `Ctrl` + `Shift` + `F` |
| Toggle the Side Bar | `Ctrl` + `M` (the physical `Ctrl` key, also on macOS) |
| Show or hide the Output View | `F1` |
| Session Terminal (a terminal below the agent) | `Ctrl` + `` ` `` (`Control` + `` ` `` on macOS) |
| Archived Sessions | `Ctrl` + `Shift` + `A` |
| Start Review Buddy | `Ctrl` + `Shift` + `R` |
| Goto Anything | `Ctrl` + `Shift` + `P` |
| Switch to the Next Page | `Ctrl` + `Tab` (the physical `Ctrl` key, also on macOS) |
| Switch to the Previous Page | `Ctrl` + `Shift` + `Tab` (the physical `Ctrl` key, also on macOS) |
| Restart (Refresh) the Current Session | `F5` |
| Rename Session | `F2` |
| Find | `Ctrl` + `F` |
| Find Next / Previous | `F3` / `Shift` + `F3` |
| Go to Line | `Ctrl` + `G` |
| Save the Open File | `Ctrl` + `S` |
| Close the Editor | `Ctrl` + `W` |

The page switcher lists every open session, terminal and file, most recently used first. Keep
`Ctrl` pressed and press `Tab` or `Shift`+`Tab` to move through the list, then release `Ctrl` to
switch (`Esc` cancels).

---

## Session Persistence & Restore

Kennel remembers your work between runs. At the next start, it restores each session and, where
the agent supports it, resumes the conversation with the native resume flag of the agent
(configured as **Resume Args**):

| Agent | Resume |
|---|---|
| Claude Code | `claude --continue` |
| OpenAI Codex | `codex resume --last` |
| Kiro CLI | `kiro-cli chat --resume` |
| OpenCode | `opencode --continue` |

Plain terminals return with their last folder and shell, and open files return as well (unsaved
edits are not kept). Closing Kennel, or rebooting the machine, does not cost your agent its
context.

Sessions that run in tmux (see [Sessions in tmux](#sessions-in-tmux)) do more than resume: the
agent **kept running** while Kennel was closed, and the restore **attaches** to it. The resume
option is used only when the tmux session is gone. A session on a machine that is down keeps its
tab, with a message bar, and starts after **Restart** once the machine is available again.

Sessions whose program ends on its own (with `Ctrl-D` or `exit`) are removed automatically. In
tmux, a tab is removed only when its tmux session is gone too: if it is still running, the tab
stays as **Detached**.

---

## Customization

- **Terminal themes.** Pick from a large collection of color themes in **Settings → Theme**, with
  a live preview. Changes apply at once, without a restart. The default is `Mariana`.
- **Application theme.** Choose **Follow terminal theme**, **System**, **Dark** or **Light** in
  **Settings**. Toolbars, icons and lists follow the theme.
- **Font.** Set the terminal font and size in **Settings → Change Terminal Font...**, with a live
  preview.
- **Tabs.** Set the position of the tabs and of the Side Bar tabs, the color of the tab outline,
  and a close button on every tab. A middle click always closes a tab. Right-click the tab bar
  for **Tab Position**, **Tab Height** and **Group Tabs by Session Group**.
- **Tab Height.** **Settings → Tab Height** has **Short**, **Small**, **Medium** and **Tall**, applied
  during selection (**Cancel** restores the previous height). The tabs of the Side Bar follow. The
  default is Medium on Windows and macOS, and Short on Linux.
- **The main toolbar.** The buttons are grouped on rounded pills, with the shortcut of a button in
  its tooltip.
- **tmux.** **Settings → Tmux** has the default for **Run sessions in tmux** (on by default; a
  Windows shell on this machine never uses tmux) and two lists of tmux commands, one per line, that
  are set when a session starts in tmux: one for agent sessions and one for terminals, each with
  **Reset to Default**. The defaults are `set -g mouse on`, `setw -g mode-keys vi` and
  `set -g set-clipboard on`, and for agents `set -g allow-passthrough all`
  (`allow-passthrough` is adapted to the version of tmux). The values are stored in
  `~/.kennel/.persist.json` (`tmuxEnabled`, `tmuxAgentSettings`, `tmuxTerminalSettings`).
- **Terminal preferences.** Set the size of the scrollback buffer.
- **Layout.** **View → Toggle SideBar** (`Ctrl`+`M`) shows or hides the Side Bar, leaving only
  the pages, and **View → SideBar Position** puts it on the left or on the right. **View → Reset
  Kennel Layout** restores the default layout.
- **Remote hosts.** Manage reusable SSH hosts in **Settings → Manage Remote Hosts...**.
- **Update checks.** Use **Settings → Check for Updates...** to look for a newer version.
- **Plain terminals.** Open a terminal without an agent with **File → New Terminal**
  (`Ctrl`+`E`).

All settings are stored under `~/.kennel/` and can be edited in the application.

---

## Installation

For build prerequisites, instructions for each platform and the developer guide, see
**[BUILDING.md](BUILDING.md)**.

---

## Where Kennel Stores Things

Everything is stored under `~/.kennel/`:

```
~/.kennel/
├── config.json      # Your agent definitions and job definitions
├── workspace.json   # Your sessions, open files and archived sessions
├── .persist.json    # UI preferences (window size, theme, fonts) - safe to delete
└── logs/
    ├── kennel.log   # Application log
    └── jobs.log     # Job run history (JSON Lines: one JSON object per line)
```

Kennel also installs two programs, after confirmation, in `~/.kennel/bin/` on the machine where a
session runs or where a search is made (this machine, a WSL distribution or an SSH host): `rg`
(ripgrep 15.2.0, for Find in Files) and `tmux-shim` (1.1.1, for the activity status of sessions in
tmux). Each has a pinned version and a SHA-256 checksum. Nothing else is written there.

A corrupt `config.json` or `workspace.json` recovers by itself. Kennel saves the damaged file as
`*.bak-<timestamp>` and falls back to safe defaults, so the application always starts.

Pass `-d <dir>` on the command line to store all of the above under `<dir>/.kennel` instead of
`~/.kennel`. This is useful for running several Kennel instances side by side with independent
data.

### Advanced: editing agents as JSON

Agents are stored in `config.json`, in the `agents` array. The **Manage Agents** dialog and the
wizard are the friendly front end, but you can edit the file directly:

```json
{
  "version": 1,
  "global": {},
  "agents": [
    {
      "name": "Claude Code",
      "executable": "claude",
      "baseArgs": [],
      "resumeArg": "--continue",
      "iconPath": "claude-code.svg",
      "extraArgs": [],
      "env": {}
    },
    {
      "name": "Kiro CLI",
      "executable": "kiro-cli",
      "baseArgs": ["chat"],
      "resumeArg": "--resume",
      "iconPath": "kiro.svg",
      "extraArgs": [],
      "env": {}
    },
    {
      "name": "Remote Builder",
      "executable": "kiro-cli",
      "baseArgs": ["chat"],
      "resumeArg": "--resume",
      "iconPath": "builder.svg",
      "extraArgs": [],
      "remoteHost": "dev-box.example.com",
      "remoteUser": "user",
      "env": {
        "PATH": "/home/user/python3/bin:$PATH"
      }
    }
  ]
}
```

Set `remoteHost` and `remoteUser` to run an agent over SSH.

---

## Troubleshooting

**A session does not start.** Make sure that the **Executable** of the agent is on your `PATH`
(or use an absolute path). The exact command and the error are in `~/.kennel/logs/kennel.log`.

**A remote (SSH) agent cannot connect.** Confirm that you can run `ssh <user>@<host>`
non-interactively (with key-based authentication). Kennel uploads a small helper and runs the
agent over an interactive SSH session.

**A session says that tmux is not installed (or is too old).** Install tmux 3.3 or newer on that
machine with its package manager (for example `apt install tmux`, `brew install tmux`), then use
**Restart** (`F5`). Kennel does not install tmux. On Windows, a native shell never uses tmux.

**A session in tmux keeps running after I closed it.** Only **Close** stops a session; **Archive**
and quitting Kennel keep it. To stop one by hand: `tmux -L kennel kill-session -t <name>` (terminals:
`-L kennel-term`), on the machine where it runs. `tmux -L kennel ls` lists them.

**A tab says Detached.** Its terminal ended (for example `Ctrl+b` `d` and `exit`, or a dropped
connection) while the tmux session is still running. Use **Restart** (`F5`) to attach again, or
close the tab to stop the session.

**There is no activity icon in a session in tmux.** tmux-shim is not on that machine: it is not
Linux x86_64 or aarch64 (macOS, for example), or the check or the install failed (the log says
why). The session works as usual in tmux, without the icon.

**Find in Files finds nothing, or asks to install ripgrep.** Kennel uses its own `rg` in
`~/.kennel/bin` on the machine that is searched. A host that runs Windows over SSH is not supported.

**Sessions do not return after a restart.** Verify that `~/.kennel/workspace.json` exists and is
readable. If it was corrupt, look for a `workspace.json.bak-<timestamp>` backup.

**The panes are missing or in the wrong place.** Use **View → Reset Kennel Layout**.

**Theme colors look wrong.** Select a built-in theme in **Settings → Theme** and check the log
for theme-loading errors.

---

## License

Kennel is licensed under the **BSD 3-Clause License**. See [`LICENSE`](LICENSE).

## Author

**Eran Ifrah**: [GitHub](https://github.com/eranif)

## Related Projects

- [CodeLite IDE](https://codelite.org): the cross-platform, open-source IDE.
- [wxTerminalEmulator](https://github.com/eranif/wxTerminalEmulator): the embedded terminal
  control that powers the sessions of Kennel (fetched automatically at build time).
- [wxWidgets](https://www.wxwidgets.org): the cross-platform GUI toolkit that Kennel is built on.
