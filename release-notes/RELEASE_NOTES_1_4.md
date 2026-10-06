# Kennel 1.4.0 Release Notes

*Covers changes since tag `1.3.0`.*

Kennel 1.4.0 makes the terminal's file links useful: Ctrl+Click on a file path opens it in
the built-in editor, including files that live on a remote host. Scheduled jobs gain a
"daily at a given time" mode, and a large internal cleanup of how workspaces and sessions are
tracked fixes several crashes and naming limitations.

## Major Improvements

### Open files straight from the terminal

- **Local files** — Ctrl+Click on a file path printed in a terminal now opens text files in
  Kennel's built-in editor (with syntax highlighting and Save / Ctrl+S). Binary or very large
  files (over 16 MB) still open in the OS default application.
- **Relative paths now work** — paths are resolved against the session's launch directory
  (falling back to your home directory) instead of Kennel's own process directory, and a
  leading `~/` is expanded. Previously, most paths printed by commands such as `ls`,
  `git status` or compiler output failed to open.
- **Remote files over SFTP** — in a remote (SSH) session, Ctrl+Click on a path now downloads
  the file over SFTP and shows it in a read-only viewer, with syntax highlighting chosen by file
  extension. The download runs on a background thread so the UI never freezes. Binary and
  oversized files are reported with a message rather than displayed.

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

- The `WorkspaceManager` class was removed; the UI tree is now the single source of truth and
  the workspace is persisted from it through `WorkspaceStore`. `SessionGroup` became a plain
  data holder.
- **libssh 0.11.5** is now fetched with CMake `FetchContent` from the Jarod42 mirror and built
  as a static library on every platform. OpenSSL and zlib come from the system.
- `wxTerminalEmulator` was updated to a newer revision.

## Upgrade Notes

- **New build dependency: OpenSSL.** Windows: `pacman -S mingw-w64-clang-x86_64-openssl`.
  macOS: `brew install openssl@3`. Linux: install your distribution's OpenSSL and zlib
  development packages. See [BUILDING.md](../BUILDING.md).
- The Windows installer now also ships `libcrypto-3-x64.dll`, `zlib1.dll` and
  `libwinpthread-1.dll`.
- Remote file opening requires key-based SSH login (ssh-agent, or an unencrypted default key
  in `~/.ssh`). A host not yet in `known_hosts` is trusted and recorded on first connect; a
  changed host key is refused.
- Remote relative paths are resolved against the session's working directory, then
  `$HOME/.kennel/sessions`, then `$HOME`, since the remote shell's current directory isn't known.
- The macOS and Linux builds with libssh have not yet been verified.

---

Previous release: [1.3.0](RELEASE_NOTES_1.3.0.md).
