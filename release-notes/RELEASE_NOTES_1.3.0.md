# Kennel 1.3.0 Release Notes

*Covers changes from `1.2.0` to `1.3.0`.*

Kennel 1.3.0 is a big one: the main window's navigation model was reworked around a
single session tree, a full scheduled-jobs system was added end to end (definition,
execution, history, and a log viewer), and a long list of stability and usability fixes
landed along the way.

## Major Improvements

### A single session tree replaces group tabs

The main window's left pane is now one tree — **Groups → Sessions** — instead of a flat
group list paired with per-group tabs. Selecting any session in the tree shows its
terminal in the single detail pane on the right. `SessionGroup` is now a plain data
holder; `MainView` owns the tree and drives selection, rename, move-to-group, and
close/refresh directly against tree items. Clicking a group header only expands or
collapses it — it no longer changes which session is displayed.

Session cycling was simplified to match: instead of first cycling *groups*, then
sessions within a group, **Alt+←/→ now cycles through every open session**, regardless
of which group it's in.

### Scheduled Jobs

Kennel can now run things on a timer, even while you're not watching:

- A **job** either runs a raw shell command or sends a one-shot prompt to a configured
  agent running in non-interactive mode, on a fixed interval (in hours).
- Jobs launch into a dedicated **Jobs** session group; these are one-shot runs and are
  not persisted to the workspace like regular sessions.
- **Jobs → Manage Jobs...** opens a CRUD dialog for creating, editing, deleting,
  enabling/disabling, and manually running (**Run Now**) job definitions. Manage Jobs
  now tracks unsaved changes and asks for confirmation before discarding them (Cancel
  button, Escape, or the window's close box).
- Jobs can be individually **enabled or disabled**; a disabled job is skipped by the
  scheduler but its next-run time still advances, so it won't fire a backlog of missed
  runs the moment it's re-enabled.
- **Run Now** always saves any pending edits first, then focuses the new session's
  terminal tab — jobs fired automatically by the scheduler run silently in the
  background without stealing focus.
- Agent definitions gained a **non-interactive switch**, used to build the one-shot
  command line for prompt-type jobs.

### Job run history & log viewer

Every job run now appends a JSON-Lines record (start / end / failed) to
`~/.kennel/logs/jobs.log`, independent of the main `kennel.log`, so scheduled and manual
runs leave a durable, append-only trail.

**Jobs → View Job Log...** opens a dialog listing every parsed record newest-first, with
a live substring filter box and a detail view (double-click a row, or press Enter) that
shows the full record as pretty-printed, syntax-highlighted JSON in a read-only viewer.
Malformed lines are skipped rather than failing the whole read.

### Duplicate Session

Sessions can now be cloned: right-click a session and choose **Duplicate...** to open
**Start Agent** pre-filled with the same agent, group, and a generated unique name —
handy for spinning up a fresh session with the same setup.

### Run multiple Kennel instances side by side

A new `-d <dir>` command-line option overrides the data-home directory (Kennel data is
then stored under `<dir>/.kennel` instead of `~/.kennel`), so you can run multiple
Kennel instances concurrently, each with its own independent set of sessions and
config.

### Read-only file/text viewer

A new `ReadOnlyFileViewer` (built on the existing text editor component) provides a
streamlined, non-editable viewing experience — no toolbar, closes with Escape, centered
on its parent. It's the viewer used by the job log's detail view, and is available
generally for showing files or generated text without risking accidental edits.

## Additional Functionality

- **Rename via context menu for agent sessions** — previously only available via `F2`
  for the selected item; now every session (plain terminal or agent) exposes
  **Rename...**/**Rename Terminal** in its right-click menu.
- In-place (double-click-to-edit) renaming directly in the tree control was disabled in
  favor of the validated rename dialog, which checks for name collisions and persists
  the change to the workspace.
- The Edit menu's `F2` entry was relabeled **"Rename Session..."** (it always renames
  the selected session, never a group — the old "Rename Group" label was misleading).
- The **Start Agent** dialog's working-directory option is now optional, with a
  simplified layout.
- `StartAgent` falls back to the currently selected group when no explicit group is
  given.

## Bug Fixes

- **Remote session working directory** — remote agents now default to
  `$HOME/.kennel/sessions` on the remote host instead of an incorrect path.
- `SessionPage::IsActive` now correctly determines activity from the tree's currently
  selected group/session rather than a stale notebook-page check.

## Upgrade Notes

- Workspace and config formats are unchanged and forward-compatible; no manual migration
  is needed.

---

See [README.md](../README.md) for the full feature overview, and
[RELEASE_NOTES_1.2.0.md](RELEASE_NOTES_1.2.0.md) for the previous release.
