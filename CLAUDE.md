# Working in this repo

## HARD RULE, above all others

**Do EXACTLY what the user tells you. NEVER assume. NEVER put something somewhere
"for convenience". Do NOT make decisions: ASK.** A class, a place, a name, an approach, a
behaviour that the user did not give is a decision, and it is the user's: stop and ask one short
question first.

Kennel is a native desktop app (C++/wxWidgets) for running and managing multiple
interactive AI CLI agent sessions side by side. Full architecture, build, and
contributing details live in [`BUILDING.md`](BUILDING.md) and [`README.md`](README.md) —
read those for depth. This file covers the rules an agent must not violate.

In addition, if you have't done so, read @CLAUDE.md

## Hard rules

- Load the wxcrafter skill before editing any *.wxcp file or any file generated from one. Generated files carry the banner "Do not modify this file by hand!" and are overwritten on every run, so never edit them directly: change the .wxcp as the skill describes, then regenerate.
- **ALL UI goes through wxCrafter.** A new dialog, panel, page or control is a window in
  `src/app/UI.wxcp` (the `wxcrafter` skill: read its references, copy a sibling widget, run
  `wxcgen`), with its logic in the generated "Inherited Class" (`wxcgen` writes the skeleton).
  Never build a dialog or a page by hand in C++ (`new wxDialog`, `new wxChoice` and sizers in a
  constructor), not even a small one. Only what cannot be designed (a custom drawn control, a
  popup, the content of a list) is made in code.
- **Never undo the user's changes to any wxCrafter file (`*.wxcp`), or to any file
  generated from one.**
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
- **NEVER name a function `XxxOf(...)` or `XxxFor(...)`.** Not `DistroOf`, `ClientKindOf`,
  `TmuxTargetOf`, `DefaultIconFor`, `SessionNameFor`, `NodeOf`: the name never says what
  comes back or what the argument is. Write a verb that says it: `GetDistroNameFrom(shell)`,
  `GetClientKindFrom(executable)`, `IsXxx()` for a bool, `GetXxx()` for a value that is held
  or worked out, `FindXxx()` for a lookup that can fail. `For` is allowed only when it is
  absolutely needed, for example when a wxWidgets API we override or call has it
  (`GetBitmapFor`). The existing `XxxOf` / `XxxFor` functions are debt, to be renamed: do not
  copy them, and rename one when you touch the code around it.
- **A method on the object, NOT a global helper.** If the question or the work is about one
  of our types (`Session`, `Account`, `AgentDef`, a page), it is a **non-static
  method of that type**: `session.IsTmuxSession()`, `session.GetTmuxTarget(...)`,
  `agent.GetDefaultIcon()`, not `DefaultIconFor(agent)`, not a free function, not a `static`
  of some other class. Before you write a free function or a `static` that takes such an
  object as its main argument, put it on the object. A free function is only for something
  that no type owns (and then it is named by the rule above). A `static` method is the last
  resort, not the first.
- **Do NOT put helpers in an anonymous namespace (or file-level `static`) unless you have
  no choice.** It has become abuse: every `.cpp` collects private functions that nobody can
  reuse, test or find. Use a **private non-static method** of the class (it can reach the
  members, and its name is in the header), a method of the object it works on (see above),
  or a small class of its own (see "Conventions", simple single-purpose classes). Not a
  `static` private method either, unless it really needs no object. An anonymous namespace
  is for what C++ forces into one: a constant or a type that only this file uses, a
  `wxDEFINE_EVENT`-style registration, or a callback with a fixed C signature. If you are not
  sure that you have no choice, you have one. The existing anonymous namespaces are debt, to
  be cleaned up: do not add to them, and move a function out when you touch it.
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

**When a change breaks something the code relied on, find every user of it before
calling the work done.** A page that always had a terminal can now exist without one; a
function that was synchronous is now asynchronous; a value that was never empty can be;
a status gains a new state. Before building, grep the whole repo for every use of the
thing that changed (`GetTerminal()`, `m_terminal`, the status enum, the old function) and
read each hit, in `src/app/` as well as the file you edited. Then trace the first code
that runs at startup for the case you changed (restoring sessions, loading the workspace),
not only the path where the user creates something new. A green build says nothing about
this. In the report, list the paths you read, and say which you did not.

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

# Rules for agents working on Kennel

- **Ask, do not assume.** If the request is ambiguous, or you are about to
  choose something that the user did not specify (a class, a library, an
  approach, where a feature lives), stop and ask one short question first.
- **Do what was named.** If the user names a class or an approach (for example
  `wxAuiNotebook`), use that one. Do not substitute another, and do not add
  extra changes to other files or classes that were not asked for.
- **Do not state what you have not read.** Open the code before saying what a
  class is or does.
- **Find every user of what you change.** When a change breaks something the
  code relied on (an object that always had a terminal now may not, a call is now
  asynchronous, a new state), grep the whole repo for its users and read them, and
  trace the startup path (restoring sessions) before saying it is done. See
  `CLAUDE.md`, "Build & verify".
- **NEVER name a function `XxxOf(x)` or `XxxFor(x)`** (`DistroOf`, `TmuxTargetOf`,
  `DefaultIconFor`). Say what it returns with a verb: `GetDistroNameFrom(shell)`,
  `IsXxx()`, `GetXxx()`, `FindXxx()`. `For` only when it is absolutely needed (a wxWidgets
  API). The old ones are debt: do not copy them. See `CLAUDE.md`, "Hard rules".
- **A method on the object, NOT a global helper.** If it is about one of our types
  (`Session`, `Account`, `AgentDef`, a page), it is a non-static method of that type:
  `session.IsTmuxSession()`, `agent.GetDefaultIcon()`, not `DefaultIconFor(agent)` and not
  a `static` somewhere else. A `static` method is the last resort.
- **Do NOT use an anonymous namespace (or a file-level `static`) for helpers** unless you
  have no choice. Use a private non-static method, a method of the object, or a small
  class. If you are not sure that you have no choice, you have one. The existing ones are
  debt: do not add to them. See `CLAUDE.md`, "Hard rules".
- Load the wxcrafter skill before editing any *.wxcp file or any file generated from one. Generated files carry the banner "Do not modify this file by hand!" and are overwritten on every run, so never edit them directly: change the .wxcp as the skill describes, then regenerate.
- **All UI goes through wxCrafter.** A new dialog or page is a window in `src/app/UI.wxcp`,
  made with the `wxcrafter` skill and `wxcgen`: never hand-write one in C++. See `CLAUDE.md`,
  "Hard rules".
- **Just change the code.** No throwaway test programs or extra verification:
  make the change, build, and report what is not verified.
- **Do not commit or push** until the user says so.
- Building toolbars: when adding a toolbar item that has a keyboard shortcut (e.g. "Ctrl-Shift-A") the keyboard shortcut
 must appear in the toolbar item's tooltip. On a separate line.

