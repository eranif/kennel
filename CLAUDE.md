# Working in this repo

Kennel is a native desktop app (C++/wxWidgets) for running and managing multiple
interactive AI CLI agent sessions side by side. Full architecture, build, and
contributing details live in [`BUILDING.md`](BUILDING.md) and [`README.md`](README.md) —
read those for depth. This file covers the rules an agent must not violate.

## Hard rules

- **Never hand-edit generated wxCrafter files: `src/app/UI.hpp`, `src/app/UI.cpp`.**
  They are generated from `src/app/UI.wxcp` by wxCrafter. If a change requires touching
  the UI layout (adding/removing/renaming a widget, changing a choice's items, sizer
  structure, an event binding), **load the `wxcrafter` skill** (Skill tool) and follow
  it: edit `src/app/UI.wxcp` with the smallest change, then regenerate with `wxcgen`.
  Never edit `UI.hpp`/`UI.cpp` themselves. How to do it here:
  - Check `git status` first. If `UI.wxcp`, `UI.hpp` or `UI.cpp` already have
    uncommitted changes (the user may be working in wxCrafter), stop and ask before
    regenerating: it overwrites the generated files.
  - Run it from `src/app`: `wxcgen --bitmaps-in-base-class-file UI.wxcp` (the flag keeps
    the bitmap code in `UI.cpp`, as it is checked in; without it a `UI_app_bitmaps.cpp`
    appears: delete it and run again with the flag).
  - Then run `clang-format -i src/app/UI.cpp src/app/UI.hpp` (see "Build & verify"),
    and review `git diff`: only the intended change should show. The XRC resource name
    near the top of `UI.cpp` carries the path of the machine that generated it, so it
    may differ: that is expected.
  - If `wxcgen` is not installed, tell the user what to change in wxCrafter instead.
  - An event handler or a binding that the dialog's own code can add (for example
    `Bind(wxEVT_CHOICE, ...)` in the constructor of the subclass) needs no change of
    the `.wxcp`.
  Everything else in `src/` is fair game to edit directly.
- **Never undo the user's changes to any wxCrafter file (`*.wxcp`), or to the files
  generated from them (`src/app/UI.hpp`, `src/app/UI.cpp`, and any other generated file).**
  Do not revert, restore, check out, stash, or overwrite them, even when they are
  uncommitted, and even when a review or a fix request asks for it. If a review asks for
  such a revert, leave the files as they are and explain the conflict in the response.
- **No C++ exceptions.** Errors flow through `Status` / `StatusOr<T>` (`core/Status.h`),
  absl-style. Don't introduce `throw`/`try`/`catch`.
- **`src/core/` stays GUI-free.** It uses only wxBase types (`wxString`, `wxFileName`,
  etc.), no wxWidgets UI classes. UI logic belongs in `src/app/`.
- **ALWAYS use wxWidgets events for notifications and callbacks.** Avoid `std::function`.
- **Logging goes through `KLOG_DEBUG()/KLOG_INFO()/KLOG_WARN()/KLOG_ERROR()`**
  (`core/Logger.h`), never `wxLog*`. Output goes to `~/.kennel/logs/kennel.log`.
- When git-committing a change, make sure to sign-off the commit using git's "-s"
- NEVER add a "Co-Authored-By:" line to the commit message.

## Build & verify

```bash
./build.sh      # first run: also bootstraps wxWidgets into .build-release/wxWidgets-install (slow)
./build.sh      # subsequent incremental builds, once configured
```

Always build after a change before calling it done. wxWidgets headers for reference are
under `.build-release/wxWidgets-install/include/wx-3.3/wx/`. There is no test suite;
build success plus manual verification (see the `run` skill) is the bar.

Format before committing:

```bash
find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) ! -name 'json.hpp' \
  | xargs clang-format -i
```

## Layout

- `src/app/` — wxWidgets UI. `MainFrame` (window/menu/toolbar) -> `MainView` (glue: owns the
  `PageRegistry`, which holds the groups and their pages, the flat list `FlatView` and
  the single visible page) -> `SessionGroup` (plain data: a group's name + its
  sessions) -> `SessionPage` (wraps one `wxTerminalViewCtrl`/agent process) or
  `FilePage` (an open file).
- `src/core/` — GUI-free: paths, config, agent registry, workspace/session persistence
  (`workspace.json`), logging. Corrupt JSON self-recovers (backs up as
  `*.bak-<timestamp>`, falls back to defaults) — don't remove that behavior.

## Conventions

- Methods: PascalCase. Strings: `wxString` in interfaces/storage, convert at the
  JSON/std boundary (`ToStdString(wxConvUTF8)` / `wxString::FromUTF8`). Paths:
  `wxFileName`. JSON: `nlohmann/json` vendored at `src/core/json.hpp` — include only in
  `.cpp` files, never in public headers.
- **Prefer simple, single-purpose classes.** Name a class for what it does, e.g.
  `RemoteInstaller` (downloads a file from a repository over a web request, then
  uploads it to a target folder), rather than a large class that does several
  jobs. Keep these generic where a future feature will reuse them (the installer
  must work for any tool and any target folder, not just `rg` in `~/.kennel/bin`).
- **Don't store state that the runtime can answer.** If the source of the truth
  has a query (`process->IsAlive()`, `file.Exists()`, `window->IsShown()`), call
  it at the point of use. Do not keep a mirrored flag (`m_isAlive`, `m_exists`,
  `m_shown`) that has to be kept in sync. Keep a member only for state nothing
  else holds (a user choice, a request in flight, a cache that is
  deliberately a cache).
- Don't push to `origin` or amend/force anything without explicit user approval for that
  specific action, per the global safety rules already in effect for this session.
