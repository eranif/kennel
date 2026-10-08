# Kennel 1.4.0 Release Notes

*Covers changes since tag `1.3.0`.*

Kennel 1.4.0 makes the terminal's file links useful: Ctrl+Click on a file path opens it in
an editor page inside Kennel, including files that live on a remote host, which can be edited
and saved back over SFTP. Scheduled jobs gain a
"daily at a given time" mode, and a large internal cleanup of how workspaces and sessions are
tracked fixes several crashes and naming limitations.

## Major Improvements

### Open and edit files straight from the terminal

Ctrl+Click on a file path printed in a terminal opens the file in Kennel itself. Files are
listed under a new **Files** container in the session tree (like **Terminals**), and each one
opens as an editor page in the main view instead of a modal dialog.

- **Local files** — text files open in the built-in editor with syntax highlighting. Edit and
  save with Ctrl+S (Cmd+S on macOS). Binary or very large files (over 16 MB) still open in the
  OS default application.
- **Relative paths now work** — paths are resolved against the session's launch directory
  (falling back to your home directory) instead of Kennel's own process directory, and a
  leading `~/` is expanded. Previously, most paths printed by commands such as `ls`,
  `git status` or compiler output failed to open.
- **Remote files over SFTP** — in a remote (SSH) session, Ctrl+Click on a path downloads the
  file in the background and opens it for editing. Ctrl+S uploads it back in the background,
  with a "Saving..." indicator, so the UI never freezes. Saves go to a temporary file that is
  renamed over the original, keeping the file's permissions, so a failed save never leaves a
  half-written file. A file that is not valid UTF-8 opens read-only, since saving it would
  corrupt it. Binary and oversized files are reported with a message instead of being shown.
- **One page per file** — clicking a file that is already open selects its page. Closing a
  file with unsaved changes asks whether to save. The Files container has a **Close All Files**
  menu entry, and each file has a **Close** entry.
- **Navigation and appearance** — theme and font changes apply to file pages too.
- **Flat page list** — below the session tree, a second list shows every open terminal,
  session and file in one place (icon and name, plus its group), most recently used first.
  It selects pages and offers the same context menus as the tree (right-clicking empty space
  offers Start Agent and New Terminal). Choosing a page in this list, with the mouse or the
  keyboard, does not change the recent order, so the list never reshuffles under your hand;
  anything else (the tree, Ctrl+Tab, opening a file or session) does. The recent order is
  saved in `workspace.json`, so after a restart Kennel reopens on the page you used last.
- **Ctrl+Tab page switcher** — Ctrl+Tab / Ctrl+Shift+Tab (the Ctrl key on every platform,
  including macOS) replaces Alt+Left/Right. It pops up a list of every open terminal, session
  and file, most recently used first; keep Ctrl held and press Tab / Shift+Tab to move through
  it, and release Ctrl to switch. Escape cancels. On macOS, which claims Ctrl+Tab for moving
  the keyboard focus, it is a menu key equivalent (**Search → Select Next Page**) and the popup
  registers hot keys while it is open.

### Review Buddy: a second agent that reviews the first

Right-click in an agent's terminal and choose **Launch Review Buddy** and an agent (Kiro,
Claude, ...). The session splits in two: your agent on the left, the reviewer on the right.
Kennel then runs this cycle, with no more input from you:

1. The reviewer reviews all **unpushed** work: uncommitted and staged changes, new files, and
   commits that are not on the upstream branch.
2. When it is done, your agent is told to read the review and fix it.
3. When your agent is done, the reviewer reviews again. This repeats until the reviewer says
   `STATUS: CLEAN`, up to 5 rounds.

The agents tell Kennel that they are done by writing marker files in
`.agents/reviews/<id>/` in the session's folder (for example `review-completed-2.marker`, where
the number is the round). All rounds use that one folder, because some tools ask for
permission each time they write into a new folder. This works the same for every agent
and over SSH, and it does not matter what you do in the terminals (switching agents,
scrolling, ...). Kennel types a single line into each terminal that points to a request file
with the details. The reviewer may only
write under `.agents/reviews/`. On a local repository, Kennel adds that folder to
`.git/info/exclude`.

When the loop ends (the review is clean, the round limit is reached, or something fails),
Kennel tells you in four ways: a message bar at the top of the session that stays until you
close it; the result in the status bar; a system notification when you are not looking at
that session; and, when Kennel is in the background, a bouncing Dock icon (macOS) or
a flashing taskbar button (Windows). The context menu shows the state and offers **Send the
Request Again**, **Stop the Review**, **Open the Latest Review** and **Close Review Buddy**.
After 20 minutes without an answer, Kennel notifies you the same way but keeps waiting.

The entry needs a `.git` folder (or file) in the session's working directory, and only offers
agents that run on the same host as the session.

- **New terminal context menu** — the terminal's right-click menu is now Kennel's own (Copy,
  Paste, Clear buffer, and the review entries).
- **Border around terminals** — every terminal has a 5 pixel border in the theme's background
  color.

### A better built-in editor

The editor used for opened files (and the job log viewer) gained:

- **Line numbers** — a margin that resizes with the file, separated from the text by a thin
  line, in the active theme's colors.
- **Syntax highlighting for many more languages** — CMake, bash/shell, Markdown, Java, XML,
  Ruby, TypeScript, JavaScript, Python and Makefile, in addition to C/C++ and JSON. The
  language is chosen from the file name or extension, including `CMakeLists.txt`, `Makefile`,
  `Rakefile`, `Gemfile` and shell dotfiles such as `.bashrc` and `.zshrc`.
- **Language-appropriate indentation** — Makefiles use real tab characters (recipe lines
  require them) and Python uses 4-space indentation.

### Daily scheduled jobs

Jobs can now run once a day at a wall-clock time (for example every day at 10:00) as an
alternative to a fixed interval in hours.

- The Job dialog has a new schedule-mode choice and a time picker.
- The Manage Jobs list describes each job as "every Nh" or "daily at HH:MM".
- Existing jobs keep their current behavior; no migration is needed.
- A daily run is skipped, not caught up, if Kennel isn't running at that time.

### Manage Jobs dialog redesign

The Manage Jobs dialog now uses a multi-column list showing each job's name, type, schedule,
**next run time**, terminal handling and enabled state, instead of a single text line.

## Additional Functionality

- **Session names are unique per group** — two groups can now each have a session with the
  same name. Name-collision checks (start, duplicate, rename, move, jobs) take the group into
  account, and new working directories are prefixed with the group name so same-named sessions
  in different groups don't collide on disk.
- **Job Log Viewer** now has a working Close button.
- **Remote connections honour `~/.ssh/config`** (host aliases, `HostName`, `Port`,
  `IdentityFile`, `User`).

## Bug Fixes

- **Schedule edits take effect immediately** — previously, changing a job's schedule had no
  effect until after the old schedule would have fired (switching from "every 720 hours" to
  "daily at 09:00" left the job idle for 30 days). A job whose schedule changed is now
  re-anchored from the current time.
- **Hand-edited `config.json` is validated** — out-of-range `intervalHours`, `dailyHour` and
  `dailyMinute` values are clamped and logged instead of producing an unusable schedule.
- **macOS crash when closing a session** — the fallback-session selection is now deferred so
  the native outline view is never accessed after its tree item has been deleted.
- **Stale last-active session** — a group now remembers its last active session by name
  rather than by pointer, avoiding dangling references when a session is closed or renamed.

## Under the Hood

- `MainView` was split up: the session tree and the new flat list are now `TreeView` and
  `FlatView` panels that report clicks and context-menu requests to `MainView` as events, and
  the context menus live in their own source file.
- The `WorkspaceManager` class was removed; the UI tree is now the single source of truth and
  the workspace is persisted from it through `WorkspaceStore`. `SessionGroup` became a plain
  data holder.
- **libssh 0.11.5** is now fetched with CMake `FetchContent` from the Jarod42 mirror and built
  as a static library on every platform.
- **OpenSSL on macOS is now vendored**: a static OpenSSL 3.3.4 is built from source at configure
  time (via `eranif/openssl-cmake`, fetched with `FetchContent`) into `.build-release/local_builds`,
  so the app bundle has no Homebrew runtime dependency. The first macOS build takes a few
  minutes longer; later builds skip it. Windows and Linux use the system OpenSSL (on Linux a
  second, vendored copy crashes because another library in the process uses the system one).
- `wxTerminalEmulator` was updated to a newer revision.

## Upgrade Notes

- **New build dependency: OpenSSL.** Windows: `pacman -S mingw-w64-clang-x86_64-openssl`.
  Linux: install your distribution's OpenSSL and zlib development packages (e.g. `libssl-dev`,
  `zlib1g-dev`). macOS needs nothing extra: OpenSSL is built automatically. See
  [BUILDING.md](../BUILDING.md).
- The Windows installer now also ships `libcrypto-3-x64.dll`, `zlib1.dll` and
  `libwinpthread-1.dll`.
- Remote file opening requires key-based SSH login (ssh-agent, or an unencrypted default key
  in `~/.ssh`). A host not yet in `known_hosts` is trusted and recorded on first connect; a
  changed host key is refused.
- Remote saves only update files that already exist, and a server without OpenSSH's
  `posix-rename` extension will refuse the final rename (the original is left untouched).
- Quitting Kennel does not yet warn about unsaved edits in open files.
- Remote relative paths are resolved against the session's working directory, then
  `$HOME/.kennel/sessions`, then `$HOME`, since the remote shell's current directory isn't known.
- The macOS and Linux builds with libssh have not yet been verified.

---

Previous release: [1.3.0](RELEASE_NOTES_1.3.0.md).
