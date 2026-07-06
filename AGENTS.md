# AGENTS.md — Kennel

> Handoff / quick-restart instructions for AI agents working on this project.
> Read this first, then load the planning context (see "Resume" below).

## What this project is

**Kennel** is a small, cross-platform (Linux/macOS/Windows) **wxWidgets** desktop
GUI app that manages multiple interactive AI command-line clients
(e.g., `kiro-cli`, `claude-code`) from one window — a "kennel" where your AI
clients live between sessions. Each client runs in a PTY rendered by an embedded
`wxTerminalEmulator` control. The app supports named, persisted sessions with
auto-save and native-resume-based context restore.

## Current status

- **Phase**: Implementation (Prompt-Driven Development).
- **Done**: Requirements clarification, detailed design, and
  `implementation/plan.md` (17 steps).
- **Implementation progress**:
  - [x] Step 1 — CMake skeleton + wxWidgets app + `wxTerminalEmulator`
    submodule; empty "Kennel" window builds and launches.
  - [x] Step 2 — `AppPaths` + `~/.kennel/` bootstrap. `core/AppPaths` uses
    wxBase types (`wxFileName`/`wxString`), injectable home via
    `WithHome(...)`; app creates root/`sessions`/`logs` on startup.
  - [x] Step 3 — `ConfigStore` (config.json: global + adapters) with tolerant
    validation. First run writes defaults incl. built-in `kiro-cli` AND
    `claude-code` adapters. Malformed JSON is non-fatal (app falls back to
    in-memory defaults, bad file preserved for the in-app editor).
  - [x] Logger (Step 9, pulled forward) — `core/Logger` streaming logger
    (`KLOG_INFO() << ...`) writing to `~/.kennel/logs/kennel.log`. Modeled on
    the wxTerminalEmulator logger; does NOT use `wxLog*`.
  - [x] Step 4 — `WorkspaceStore` (workspace.json) + `UiPrefsStore`
    (.persist.json). Shared JSON/file helpers factored into `core/JsonUtil`.
    Corrupt `workspace.json` self-recovers (backed up to
    `workspace.json.bak-<ts>`, then empty); missing/corrupt `.persist.json`
    yields defaults (non-error). App loads both at startup.
  - [x] Step 5 — `AdapterRegistry` + `ClientAdapter`. Builds `LaunchSpec`
    (exec/args/cwd/env) applying the resume strategy (native-flag injects
    `resumeArgsTemplate` when a session id exists), `BuildSaveCommand` /
    `BuildFallbackLoadCommand`, and `{sessionId}/{savePath}/{sessionDir}/{cwd}`
    placeholder substitution. Registry built from config at startup.
  - [x] Step 6 — `AwaitingInputDetector`. Callback-driven: its `OnOutput(const
    std::string&)` matches the emulator's `SetOutputCallback` signature, so it
    plugs straight into `wxTerminalViewCtrl`. Fires a notify callback once on
    the first regex match; bounded tail buffer handles boundary-spanning
    prompts; `Reset()` re-arms it.
  - [x] Step 7 — Domain core. **Design change:** the
    `ITerminal`/`FakeTerminal` seam was dropped; we use the real
    `wxTerminalViewCtrl` directly. A session is a composite widget —
    `SessionPage : wxPanel` (wxCrafter base `SessionBasePage` in
    `src/app/UI.*`) that **holds a `wxTerminalViewCtrl` member**; the terminal
    launches its child process in its constructor.
    - `SessionPage` now takes `AppPaths` + a `ClientAdapter*` + `SessionMeta`:
      it builds the launch command via `ClientAdapter::BuildLaunch` (joining
      exec+args; cwd passed to the terminal's new `workingDirectory` ctor
      param; adapter env merged over the inherited environment), wires
      `SetOutputCallback` → an owned `AwaitingInputDetector` (fires a
      `SessionStatus::AwaitingInput` transition), maps
      `wxEVT_TERMINAL_TERMINATED` → `SessionStatus::Exited`, and exposes
      `RequestSave()` (issues the adapter save command) and `Restart()`
      (DeletePage + recreate, resuming via the adapter). A
      `SetStatusChangedCallback` hook surfaces transitions to the UI. App-layer
      `SessionStatus` enum lives in `SessionPage.hpp`.
    - `WorkspaceManager` (`core/WorkspaceManager.{h,cpp}`, GUI-free): `Load`,
      `Create` (unique-name + adapter-existence checks), `Rename` (uniqueness +
      `sessions/<name>` dir move via `wxRenameFile` + savePath rewrite),
      `Close` (registry removal + recursive snapshot-dir delete, R6.1),
      `Persist` (→ workspace.json). Wired into `KennelApp::OnInit` (replaces the
      raw `WorkspaceStore` load).
    - **Submodule change:** `wxTerminalViewCtrl` gained a 4th ctor param
      `std::optional<wxString> workingDirectory` (PTY backend `chdir`s the
      child) — this is how session cwd is honored.
    - Verified live via a temporary `MainFrame` harness (`/bin/bash` adapter):
      launch cmd built, terminal spawned, detector matched the prompt and
      fired the `AwaitingInput` transition; harness then removed.
    - **Note:** `SessionPage` has no permanent UI host yet — that arrives with
      the sidebar/terminal area (Steps 11–14).
- **Next step**: Step 8 — `SaveService` (explicit save + timed auto-save) over
  `WorkspaceManager`/`SessionPage` (see `implementation/plan.md`).
- App name **Kennel** is finalized (config dir `~/.kennel/`).

### Build conventions (this project)
- Build dirs: **`.build-debug/`** and **`.build-release/`** (git-ignored).
- CMake emits `compile_commands.json` and copies it to the workspace root on build.
- wxWidgets here is built with `wxDEBUG_LEVEL=0`; the CMake sets the matching
  define so the app links (avoids undefined `wxOnAssert`/`wxTrap`).
- Configure: `(mkdir -p .build-debug && cd .build-debug && cmake -DCMAKE_BUILD_TYPE=Debug ..)`
- Build: `(cd .build-debug && make -j12)`
- Tests are **deferred for now** (GoogleTest target not yet wired in).

## Planning artifacts (source of truth)

All under `.agents/planning/2026-06-07-ai-client-manager/`:

- `rough-idea.md` — original concept.
- `idea-honing.md` — full Q&A requirements clarification (Q1–Q23).
- `design/detailed-design.md` — **the design** (requirements R1–R15, architecture
  with mermaid diagrams, components/interfaces, data models, error handling,
  testing strategy, appendices). **Start here for technical detail.**
- `research/` — (empty; research was skipped, low risk).
- `implementation/` — (to be filled with `plan.md`).
- `summary.md` — to be generated at the end of planning.

## Resume (quick restart)

1. Load all planning docs into context:
   ```
   /context add .agents/planning/2026-06-07-ai-client-manager/**/*.md
   ```
2. Read `design/detailed-design.md` for the authoritative design.
3. Continue the PDD flow: produce `implementation/plan.md`, then `summary.md`.

## Key decisions (quick reference)

| Topic | Decision |
|---|---|
| Interaction | Embedded PTY via `wxTerminalEmulator` (one visible at a time) |
| UI | Left sidebar tree of sessions + single terminal area on the right |
| Sessions | Single-client now; workspace-grouping later; **one implicit unnamed workspace** in v1 |
| Naming | User-named, **unique**, renamable |
| Clients | **Config-driven JSON adapters** (no code change to add a client) |
| Context restore | Native **`--resume`** when possible; fallback `/chat load <path>` |
| Save | Explicit **Save** button + configurable **timed auto-save**; issues `/chat save <path>` into the terminal |
| Scrollback | On restore, **fresh output only** (no scrollback replay) |
| Close vs exit | **Close** (right-click) removes session permanently; **self-exit (Ctrl-D)** keeps it; **Restart** button relaunches |
| Awaiting-input | Adapter **regex** over terminal output → fires a **wxWidgets event** → status icon |
| Config editing | In-app via **`wxStyledTextCtrl`**, JSON validated on save |
| Logging | `~/.kennel/logs/*.log` |

## On-disk layout (`~/.kennel/`)

```
~/.kennel/
├── config.json        # global settings + adapter definitions
├── workspace.json     # the single workspace + its sessions
├── .persist.json      # UI prefs (window geometry, etc.); safely deletable
└── logs/kennel.log
```

## Tech stack / build

- **Language**: C++20
- **GUI**: wxWidgets **3.2.x and later**
- **Build**: CMake (+ CTest)
- **JSON**: nlohmann/json
- **Tests**: GoogleTest
- **`wxTerminalEmulator`**: separate repo (user-authored), included as a **git
  submodule**
- **Project type**: standalone (NOT a Brazil package)

## Architecture summary (layers)

UI (wxWidgets: `MainFrame`, `SessionPage` = `wxPanel` + embedded
`wxTerminalViewCtrl`) → Adapter (`AdapterRegistry`, `ClientAdapter`) →
Persistence + helpers (`ConfigStore`, `WorkspaceStore`, `UiPrefsStore`,
`AppPaths`, `Logger`, `AwaitingInputDetector`).

> The original `ITerminal`/`FakeTerminal` shim and a headless `Session`/
> `WorkspaceManager` domain layer in `kennel_core` were **dropped** (decision
> 2026-06-07). We drive the real `wxTerminalViewCtrl` directly, so session
> lifecycle logic lives in the GUI/app layer. `kennel_core` stays GUI-free for
> the genuinely UI-agnostic pieces (paths, config, adapters, JSON, logger,
> detector); `WorkspaceManager` (registry persistence) can still live there as
> it only touches `SessionMeta`/`WorkspaceStore`.

## Code conventions (established, MUST follow)

- **No C++ exceptions.** Code MUST NOT `throw`. Use `core/Status.h`:
  - `Status` for operations returning nothing (`Status::Ok()` /
    `Status::Error(msg)`); check `.ok()`.
  - `StatusOr<T>` (absl-style) for operations producing a value; check `.ok()`
    then read `.value()`. Accessing a value on an error result aborts.
  - Internal use of a no-throw library API (e.g. `nlohmann::json::parse` with
    `allow_exceptions=false`, or a local `try/catch` around `std::regex`
    compilation) is fine as long as no exception escapes the API surface.
- **Method naming: PascalCase** (`LogsDir()`, `PersistFile()`, `Load()`).
- **Prefer wx data types** in interfaces and storage: `wxString`, `wxFileName`,
  etc. Convert at the JSON/std boundary with `s.ToStdString(wxConvUTF8)` and
  `wxString::FromUTF8(s)` (matches the emulator submodule's idioms).
- **JSON**: nlohmann/json single header lives at `src/core/json.hpp` (vendored
  in the source tree; do NOT pull from Homebrew). Keep it out of public headers
  — confine `#include "core/json.hpp"` to `.cpp` files.
- **Logging**: use `core/Logger.h` macros (`KLOG_TRACE/DEBUG/INFO/WARN/ERROR()
  << ...`), NOT the `wxLog*` mechanism. Streaming logger (à la
  wxTerminalEmulator's `TLOG_*`); newline appended automatically; output goes to
  `~/.kennel/logs/kennel.log`. The only sanctioned `wxLogError` use is the
  pre-logger failure to create the data dir itself.
- **Formatting**: all Kennel sources MUST be formatted with `clang-format`
  before committing. The root `.clang-format` is copied from the
  wxTerminalEmulator submodule (`IndentWidth: 2`); keep the two in sync. Format
  only Kennel's own files under `src/` — never touch `third_party/`, and skip
  the vendored `src/core/json.hpp`. Command:
  ```
  find src -type f \( -name '*.cpp' -o -name '*.h' \) ! -name 'json.hpp' \
    | xargs clang-format -i
  ```

## Conventions for agents

- Treat `design/detailed-design.md` as authoritative; keep it and `idea-honing.md`
  updated if decisions change. NOTE: the design still describes the
  `ITerminal`/`FakeTerminal` seam that we dropped on 2026-06-07 — the plan's
  Step 7 note and this file are the current source of truth on that.
- Keep `kennel_core` GUI-free for genuinely UI-agnostic logic (paths, config,
  adapters, JSON, logger, detector). GUI/session-lifecycle code lives in the
  app layer and drives `wxTerminalViewCtrl` directly.
- Build/verify with CMake before committing. (Tests are deferred for now.)
- Do not commit or push unless explicitly asked.
