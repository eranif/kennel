# Working in this repo

Kennel is a native desktop app (C++/wxWidgets) for running and managing multiple
interactive AI CLI agent sessions side by side. Full architecture, build, and
contributing details live in [`BUILDING.md`](BUILDING.md) and [`README.md`](README.md) —
read those for depth. This file covers the rules an agent must not violate.

## Hard rules

- **Never hand-edit generated wxCrafter files: `src/app/UI.hpp`, `src/app/UI.cpp`.**
  They are generated from `src/app/UI.wxcp` by the wxCrafter GUI tool. If a change
  requires touching the UI layout (adding/removing/renaming a widget, changing sizer
  structure, adding an event binding), do not edit these three files yourself — tell the
  user exactly what widget/property/event change is needed in wxCrafter and let them
  make it and regenerate. Everything else in `src/` is fair game to edit directly.
- **No C++ exceptions.** Errors flow through `Status` / `StatusOr<T>` (`core/Status.h`),
  absl-style. Don't introduce `throw`/`try`/`catch`.
- **`src/core/` stays GUI-free.** It uses only wxBase types (`wxString`, `wxFileName`,
  etc.), no wxWidgets UI classes. UI logic belongs in `src/app/`.
- **Logging goes through `KLOG_DEBUG()/KLOG_INFO()/KLOG_WARN()/KLOG_ERROR()`**
  (`core/Logger.h`), never `wxLog*`. Output goes to `~/.kennel/logs/kennel.log`.

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

- `src/app/` — wxWidgets UI. `MainFrame` (window/menu/toolbar) -> `MainView` (owns the
  session tree + the single visible `SessionPage`) -> `SessionGroup` (plain data: a
  group's name + its sessions) -> `SessionPage` (wraps one `wxTerminalViewCtrl`/agent
  process).
- `src/core/` — GUI-free: paths, config, agent registry, workspace/session persistence
  (`workspace.json`), logging. Corrupt JSON self-recovers (backs up as
  `*.bak-<timestamp>`, falls back to defaults) — don't remove that behavior.

## Conventions

- Methods: PascalCase. Strings: `wxString` in interfaces/storage, convert at the
  JSON/std boundary (`ToStdString(wxConvUTF8)` / `wxString::FromUTF8`). Paths:
  `wxFileName`. JSON: `nlohmann/json` vendored at `src/core/json.hpp` — include only in
  `.cpp` files, never in public headers.
- Don't push to `origin` or amend/force anything without explicit user approval for that
  specific action, per the global safety rules already in effect for this session.
