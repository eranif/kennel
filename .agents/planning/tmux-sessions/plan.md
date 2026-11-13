# Plan: tmux based sessions

Progress: `[x]` done, `[ ]` open. Tick a box when its commit is pushed.
Status as of 2026-11-13: nothing is implemented. This is a draft: the decisions in "Open
questions" are the user's to make.

## Goal
A session can run its agent (or shell) inside a tmux session on the machine where it runs, so
that the agent **keeps running** when Kennel is closed or crashes, when the ssh connection
drops, or when the tab is closed on purpose. Opening Kennel again **re-attaches** to the live
agent instead of starting a new one with `--continue`.

## How a session starts today (what we build on)
- `SessionPage::CreateTerminal()` (`src/app/SessionPage.cpp`) starts a `wxTerminalViewCtrl` with a
  login shell (the default shell, `AgentDef::loginShell` such as `wsl.exe -d <distro>`, or a plain
  local shell), then **types the commands into it** with `SendCommand()`.
- `BuildCommandLine()` (`src/core/ClientAdapter.cpp`) makes those commands. `WrapCommand()` adds
  the `ssh -o ServerAliveInterval=10 [user@]host` line for a remote agent, then `mkdir -p && cd`,
  the `export`s, and last the agent command. Resume is `"<agent> --continue || <agent>"`.
- When the process in the terminal ends: `wxEVT_TERMINAL_TERMINATED` -> `OnTerminated()` ->
  `SessionStatus::Exited`. `Restart()` destroys the terminal and creates a new one with
  `m_resume = true`.
- A session is persisted as `Session` in `workspace.json` (`src/core/Workspace.h`). Restored
  sessions are created with `resume = true` (`MainNotebook.cpp:1653`).
- Closing a tab: `MainNotebook::CloseOrArchiveSession()` asks Archive / Close / Cancel.
- `AgentDef::IsRemote()` / `IsWSL()` say where an agent runs; `SessionPage::GetAccount()` gives
  the `Account` (local, WSL, remote).

## Design

### What runs
Per session, on the machine of the agent, **one tmux session** with one window, in a tmux server
that **only Kennel uses**: `tmux -L kennel ...` (own socket; plain terminals use a second
server, `-L kennel-term`, because their settings differ: see "Decisions", 1). Why: the user's own `~/.tmux.conf`,
key bindings and status bar do not leak into Kennel, and our options do not leak out of it.

The terminal control keeps starting the same login shell as today. We change only the **commands
that are typed** (a new function in `ClientAdapter`, next to `BuildCommandLine()`):

```
# remote: ssh ... / mkdir -p && cd / export ... as today, then:
env -u TMUX tmux -L kennel -f /dev/null new-session -A -s <name> -c <dir> '<agent command chain>' \; \
    set-option -g status off \; set-option -g set-titles on \; set-option -g window-size latest
```

The agent command chain, with the shim (settings decided by the user, 2026-11-13: Kennel's tmux
sessions use these four):

```
~/.kennel/bin/tmux-shim --config-string='set -g mouse on; setw -g mode-keys vi; set -g set-clipboard on; set -g allow-passthrough all' -- claude --continue \
  || ~/.kennel/bin/tmux-shim --config-string='...same...' -- claude
```

The four settings are **one list in Kennel** (`TmuxCommands`), rendered in two ways: as
`--config-string='...'` when the shim is used, and as `\; set-option -g ...` chained to
`new-session` when it is not (so a missing shim never loses `mouse on` or the clipboard). The
settings that need no shim (`status off`, `set-titles on`, `window-size latest`) stay on the
`new-session` line, because they should hold before the first byte is drawn.
Points to check in Phase 0: the shim applies each command at **every** start, so a restart of
the agent re-applies them (harmless); the settings change the whole `-L kennel` server, which is
ours; and that `set -g allow-passthrough all` from `--config-string` and the shim's own
`allow-passthrough on` for its pane do not fight (a pane value set by the shim is a pane
option and would override the global one: the README says the shim leaves `on` / `all` alone,
verify the order with tmux 3.4).

- `<agent command chain>` is `$HOME/.kennel/bin/tmux-shim -- <agent ...>` when the shim is
  installed (see "The shim"), otherwise the bare agent command.
- `new-session -A` = attach if it exists, else create and run the command. The command chain is
  the same `resume || fresh` chain as today, so: tmux session alive -> re-attach, agent untouched;
  tmux session gone (reboot, killed) -> start the agent with `--continue`, as today.
- Every option has `-g`: `allow-passthrough` is a pane option, and the server is ours, so
  global is right. `allow-passthrough all` (tmux 3.4+) also forwards from a tab in the
  background; on 3.3 use `on` (the probe picks by version).
  So the version probe decides the word in the `--config-string` (`all` or `on`); `all` on
  tmux 3.3 is refused by tmux (the shim reports it and the agent still starts, but the setting
  is lost).
- `-f /dev/null` + the options on the command line (`\;` on `new-session`, `--config-string` in
  the shim) means no config file has to be uploaded to the host.
- `env -u TMUX` lets Kennel run when the remote shell is itself inside the user's tmux.
- The session name is stable and unique: `kennel-<id>`, the id generated once when the session
  is created and **persisted** in `Session` (it is state nothing else holds; the name of the
  session is not unique across groups, so it cannot be derived).
- Quoting: the agent chain is one `sh -c`-safe single-quoted argument; reuse/extend the quoting
  helpers in `ClientAdapter.cpp` (the prompt quoting there is for double quotes).

### Where it can run
| Where | tmux |
|---|---|
| Remote host (ssh) | yes: the main target |
| WSL distribution | yes |
| Linux / macOS, this machine | yes |
| Windows, this machine (cmd, PowerShell, Git Bash) | **not in v1**: there is no tmux for the Windows shells. The option is off and says why. See "Local sessions". |

### Local sessions
- **Linux and macOS (this machine): fully supported, and the easiest case.** The login shell is
  local, so the typed command is the same `env -u TMUX tmux -L kennel ... new-session -A ...`
  without the `ssh` line. `TmuxClient` runs tmux with `Process` directly. tmux comes from the
  package manager (`apt`, `brew`); the probe tells the user when it is missing. Do this first:
  it exercises everything except ssh.
- **Windows, agent in WSL:** already covered (a WSL agent is a WSL account). The tmux server
  lives inside the distribution and survives Kennel closing. (A distribution that is idle may be
  shut down by WSL itself (`vmIdleTimeout`); an attached tmux client does not count, so check
  that a detached agent is not killed when the last `wsl.exe` ends: Phase 0.)
- **Windows, agent running natively (a local `claude.exe` in cmd / PowerShell / Git Bash):**
  tmux cannot host it. Three ways out, none for v1:
  1. *MSYS2 tmux* (`pacman -S tmux`, which this project's own build environment already has):
     works when the session's shell is an MSYS2 bash. Risks: a native Windows agent in an MSYS
     pty (not ConPTY) can lose raw mode / `isatty`, and the tmux server must be shown to
     outlive Kennel. Try it in Phase 0 and only then decide whether to support it, as
     "experimental".
  2. *A Kennel-owned persistence helper*: a small detached process that owns the ConPTY and a
     named pipe, which Kennel attaches to (tmux without tmux). It would also work for local
     sessions on every OS and remove the tmux dependency, but it needs `wxTerminalViewCtrl` to
     attach to a pipe instead of spawning a process. A separate, larger project: write its own
     plan if the Windows case matters.
  3. *Move the agent to WSL*: the user creates a WSL agent; nothing to build.
- Until then, on Windows a native local session shows the option disabled with the text "tmux
  needs WSL or a remote host on Windows".

### Lifecycle
- **Kennel starts (decided 2026-11-13):** each restored session that has a `tmuxName` first
  **tries to attach** to its tmux session; if there is none, it **starts a new one with the
  resume chain** (`--continue`, else fresh), exactly as a restored session does today. This is
  the one command `new-session -A -s <name> ... '<resume || fresh chain>'`: tmux attaches when
  the session exists and runs the chain only when it does not, so there is no separate "does it
  exist?" round trip and no race between the check and the start.
  - Attached: the agent is untouched (still working, still holding its conversation); Kennel
    only repaints. The tab shows the real state (the shim's next report, or "Running" from
    `ActivityMonitor` meanwhile).
  - Not there (reboot, `kill-server`, the user closed it): a new tmux session, the agent
    resumes with `--continue`.
  - Host unreachable (ssh fails, WSL not started): nothing is started and nothing is killed;
    the tab shows the error and "Restart" tries the same command again. Never create a fresh
    session because a check failed: `new-session -A` only runs on a host we reached.
  - tmux missing or too old on the host: start without tmux and say so (Phase 2 probe).
  - Several tabs, one host: the attach commands run one after the other on a worker thread per
    host (not all in the UI thread), and the tabs show "Connecting" until the terminal is up.
  - Another client is attached to the same tmux session (a second Kennel window, or the user's
    own `tmux -L kennel attach`): `-A` attaches as one more client, both see the same screen.
    Do not use `-d` (it would kick the other one off): shared, decided 2026-11-13.
- **Agent exits** -> tmux window ends -> tmux session ends -> the terminal ends -> `Exited`, as
  today.
- **Detached** (new state, no mirrored flag): the terminal ended but the tmux session is still
  there (the user pressed the detach key, ssh dropped, Kennel crashed). `OnTerminated()` asks
  `TmuxClient::HasSession()` at that moment; if alive the page shows "Detached - Restart to
  re-attach" and `Restart()` re-attaches (`new-session -A` does it).
- **Close tab** (`CloseOrArchiveSession`, decided 2026-11-13): **Close = stop**, as today: the
  tmux session is killed (`kill-session`) with the agent. No new choice in the dialog.
- **Archive = keep running:** the tmux session is detached and stays on the host; restoring the
  archived session re-attaches it (`new-session -A`; if it is gone, it resumes). Closing Kennel
  itself only detaches, never kills.
- **Restart** of a live tmux session: re-attach. No separate "Restart agent" in v1 (decided
  2026-11-13).
- **Review buddy** and **scheduled jobs** do not use tmux in v1: they are short and Kennel owns
  their whole life.

### What can break inside tmux (verify in Phase 0)
- **Program activity (OSC 7501: working / done / blocked), cwd (OSC 7), title, OSC 52 copy:** tmux
  swallows OSC it does not know. Needs `allow-passthrough on` (tmux 3.3+) **and** the sender
  wrapping the sequence in a `\ePtmux;...\e\\` DCS. Find out who sends OSC 7501 (an agent hook?)
  and whether it can wrap itself when `$TMUX` is set. Without this the spinner / check mark /
  hand on the tab stops working in tmux sessions. This is the biggest risk.
  tmux drops every sequence it does not know (confirmed by the user for OSC 9;4), and there is
  no way back for the `OSC 7501;?` query, which is the only way a program learns that the
  terminal supports the protocol. Ways out, in the order to try:
  1. **Degrade (v1, no cost):** tmux sessions use `ActivityMonitor` (running / idle) and the
     title (`set-titles on`); `TmuxClient` reads the cwd with `#{pane_current_path}`. No
     spinner / check / hand, no progress bar.
  2. **Shim in the pane (chosen, built, released; pin v1.1.1):** the agent runs as
     `tmux-shim -- <agent>`. The shim owns the agent's pty, wraps OSC 7501, 9;4, 9;9 and 7 in
     `ESC P tmux; ... ESC \` (the ESC doubled), and answers `7501;?` itself. tmux stays as is.
     OSC 52 and the title need no wrapping (`set-clipboard on`, `set-titles on`). A small static
     binary installed with `RemoteInstaller`, pinned with SHA-256. See "The shim (`tmux-shim`)".
     The same helper could later be the Windows pty host.
  3. **`pipe-pane` + a writer** that sends the OSCs to `#{client_tty}`: also needs a helper on
     the host; ordering and several clients are awkward.
  4. **tmux control mode (`-CC`):** every OSC reaches Kennel, but `wxTerminalViewCtrl` would have
     to speak the control protocol. A large change; also gives scrollback and panes. Own plan.
- **Scrolling:** the wheel scrolls the scrollback of the tmux window (copy mode), not the Kennel
  terminal's own scrollback. See `README.md` ("Mouse events for TUI applications"); decide
  between `mouse on` (tmux scrollback) and `mouse off` (wheel goes to the program).
- **Resize:** `window-size latest` so the size follows the Kennel terminal.
- **Status line:** `status off`; Kennel's own header and status bar already show the state.
- **Idle detection:** `ActivityMonitor` looks at output; a re-attach repaints the whole screen,
  which counts as output once. Check that it does not flash "Running".
- **Minimum version:** `tmux -V` >= 3.3 for passthrough; older = the option is refused with the
  version in the message.

## The shim (`tmux-shim`)
Needed for the activity icon, the progress bar and the cwd in tmux (see Findings). **Built and
released** in its own repository: <https://github.com/eranif/tmux-shim> (Rust, MIT; its
`AGENTS.md` describes the design and the rules). It runs in the pane as
`tmux-shim -- <agent> <args>`; tmux, not Kennel, starts it.

```
tmux-shim [--wrap auto|tmux|screen|none] [--no-reply] [--] COMMAND [ARGS...]
```

- **What it does:** runs the agent on a pty of its own (one thread, `poll()`, bounded buffers,
  no reader thread). Keys are copied unlooked-at. On the output: `OSC 7501;?` is removed and
  **answered by the shim**; `OSC 7501;...`, `9;4`, `9;9` and `7` are wrapped as
  `ESC P tmux ; ... ESC \`; everything else is copied unchanged, and a sequence the agent
  wrapped itself is never wrapped twice. The parser keeps its state between reads (a sequence
  can be cut anywhere). The exit code of the agent is the exit code of the shim. The pane's size
  follows to the agent; `SIGHUP` / `SIGTERM` go to the agent.
- **`--wrap auto`** (default): wraps for tmux when `$TMUX` is set, for screen when `$STY` is set,
  and **otherwise just `exec`s the agent**. So Kennel can use the same command everywhere and a
  session that is not in tmux pays nothing.
- **`allow-passthrough`:** since v1.1.0 the shim turns it on for its own pane (tmux 3.3+) when
  it is off. (v1.0.0 could not.) Kennel's tmux command still sets it globally
  (`all` on tmux 3.4+, `on` on 3.3), so hidden tabs also report. On tmux older than 3.3 nothing
  can turn it on per pane: the icon stays dark, and the probe says so.
- **Pin release v1.1.1** (<https://github.com/eranif/tmux-shim/releases/tag/v1.1.1>), static
  musl. Both hashes were checked against the downloaded binaries (2026-11-13):

  | Asset | Size | SHA-256 |
  |---|---|---|
  | `tmux-shim-x86_64-linux` | 479 920 | `4c9287b318e547a29203501db50ae45b2990e0b91509edb27ed07a56318bc39e` |
  | `tmux-shim-aarch64-linux` | 463 016 | `8f9ef771b2de3e943659f0d8b3cbc82e0b2a6b85ce15907cd4176984a5c4d4e0` |

  The names match `uname -m` and **must not change** (Kennel builds the URL from them). Each
  asset has a `<name>.sha256` file. Do **not** pin v1.0.0 or v1.1.0: v1.0.0 has no aarch64
  binary and an older behavior (it does not turn `allow-passthrough` on), and v1.1.0 has no
  aarch64 either. The version in the URL is part of the pin: a new release is a new
  `ToolDescription` (new hashes), never `latest`.
- **Options added after v1.0.0 (in v1.1.x):**
  - The shim turns `allow-passthrough` on **for its own pane** when it is `off` (tmux 3.3+),
    before the agent starts. So the `allow-passthrough` option in Kennel's tmux command is
    still wanted (`all` for hidden tabs, tmux 3.4+), but a missing one no longer silences the
    icon. `--no-tmux-setup` turns this off.
  - `-c FILE` / `--config-string 'cmd; cmd'` apply tmux commands through the shim at every
    start, in the order given; a command that tmux refuses is reported on stderr and does not
    stop the others or the agent. **Kennel uses `--config-string`** for its four settings (see
    "What runs"): `set -g mouse on; setw -g mode-keys vi; set -g set-clipboard on; set -g
    allow-passthrough all`. No file has to be uploaded. Without the shim, the same settings go
    on the `new-session` line.
- **How Kennel uses it:**
  - A `ToolDescription` for `tmux-shim` (like ripgrep's): the release URL, the asset per
    architecture, the pinned SHA-256 and the size; `RemoteInstaller` puts it in
    `$HOME/.kennel/bin/tmux-shim` (the same directory as `rg`). The binary is the asset itself,
    not an archive, so no `ReadTarGzMember` step.
  - Ensure it is installed (and has the pinned hash) before the first tmux launch on a host,
    on a worker thread, like the planned ensure-`rg` step.
  - The typed command uses the full path (`~/.kennel/bin` is not on `PATH`). The `resume ||
    fresh` chain puts the shim in front of **each** alternative:
    `$HOME/.kennel/bin/tmux-shim -- claude --continue || $HOME/.kennel/bin/tmux-shim -- claude`
    (the exit code passes through, so the chain behaves as today).
  - **If the shim is not available** (offline host, install failed, hash mismatch, an
    architecture without an asset): run the agent plainly, in tmux, and show that the activity
    icon is not available (the degrade option 1). A missing shim must never stop a session from
    starting.
  - A host that is not Linux x86_64 / aarch64 (macOS local): no pinned asset; the release has no
    macOS binary. Same fallback; add a macOS asset to the release if local macOS matters.
    Pick the asset from `uname -m` on the host (`x86_64`, `aarch64`; `arm64` on macOS is
    not served).
- **Verified** by its own tests and CI in the shim repo: the filter's unit tests (including
  "the result does not depend on how the input is cut into reads"), the tests on a real pty,
  tests with a real tmux (`TMUX_SHIM_REQUIRE_TMUX`), the oldest supported Rust, and the static
  build on x86_64 and aarch64 (the arm job runs the tests on `ubuntu-24.04-arm`).
  **Not yet verified end to end:** the shim inside real tmux with Claude Code in a Kennel tab
  (Phase 0, below). Still to do there: a speed check (`yes | head -c 1G` through the shim vs
  without).

## Phases

### Phase 0 - Spike (no commit, notes go into this file)
- [ ] By hand on a remote host and in WSL: the command above; check OSC 7, OSC 52, title, mouse,
      resize, detach / re-attach, and `claude` working inside it.
- [x] Find where OSC 7501 comes from (Claude Code sends it plain; see Findings) and try it
      wrapped in the passthrough DCS (works with `allow-passthrough on`).
- [ ] **End to end with the shim:** download `tmux-shim-x86_64-linux` v1.1.1 on a host (WSL is
      enough), run `claude` as `tmux-shim -- claude` in the command above, check the spinner /
      check / hand on the tab. Also Codex and Kiro (Kiro is where Kennel's 7501 support came
      from). Check the first-screen latency, a very long output, a resize, and that the
      tmux scrollback still works.
- [ ] Speed: `yes | head -c 1G` through the shim vs without.
- [ ] Decide the scrolling model. Write the findings under "Findings".
- [ ] Local Linux or macOS: the same command with no ssh.
- [ ] WSL: does a detached tmux session survive the distribution going idle after the last
      `wsl.exe` ends?
- [ ] Windows: MSYS2 tmux with a native `claude.exe` inside (raw mode, colours, mouse, does the
      server outlive Kennel?). Decides whether option 1 of "Local sessions" is worth a phase.

### Phase 1 - Core (`src/core/`, no UI)
- [ ] `TmuxCommands`: pure functions that build the command lines: attach-or-create, kill, list,
      has-session, version probe, and the list of the four launch settings (mouse, vi keys,
      clipboard, passthrough `all` / `on` by version) rendered as `--config-string='...'` for
      the shim or as `\; set-option -g ...` without it. Takes the session name, the folder, the command chain. Unit
      checked by hand (no test suite) with a few quoting cases: spaces, quotes, `$`, non-ASCII.
- [ ] `TmuxClient`: runs tmux on an `Account` and returns `Status` / `StatusOr` (blocking, for a
      worker thread, like `SftpClient`): `Probe()` (installed? version?), `HasSession()`,
      `ListSessions()` (name, created, attached), `KillSession()`. Remote: `SshClient::Execute`;
      WSL: `wsl.exe -d <distro> --`; local Linux/macOS: `Process`.
- [ ] `Session`: add `tmuxName` (empty = not a tmux session); persist in `WorkspaceStore`; an old
      `workspace.json` without it loads as before.
- [ ] Add the files to `CMakeLists.txt`.
- [ ] `tmux-shim` as a tool: a `ToolDescription` with the v1.1.1 assets (x86_64 and aarch64) and
      their SHA-256 from "The shim", next to the ripgrep one. Depends on `RemoteInstaller` (see
      the Find in Files plan, Phase 2), which both features share.
- [ ] `TmuxShim::Ensure(Account)`: installs / verifies the shim on the host; returns the path or
      "not available" (the caller falls back to running the agent plainly).

### Phase 2 - Launch (`ClientAdapter`, `SessionPage`)
- [ ] `BuildCommandLine()` gets the tmux form when the session has a `tmuxName` (a new parameter
      or a sibling function; keep `WrapCommand()` for the ssh / cd / export part).
- [ ] `CreateTerminal()` passes it through; the resume chain stays inside the quoted command,
      with `tmux-shim --` in front of each agent command when the shim is available.
- [ ] Probe before the first launch (on a worker thread): no tmux or too old -> the page shows
      the reason and offers to start without tmux (the session then loses `tmuxName`).
- [ ] Plain terminals (`plainTerminal`): same, a shell inside tmux.

### Phase 3 - Lifecycle
- [ ] Detached state in `OnTerminated()` (ask `TmuxClient`, do not store a flag); the message and
      `Restart()` re-attach.
- [ ] Close = `kill-session`; archive = detach and keep (no dialog change); closing Kennel only
      detaches.
- [ ] A session that is closed (not archived) must not leave its tmux session behind.
- [ ] Archived sessions that keep a tmux session running: show that in the archive list, and
      delete = kill the tmux session.

### Phase 4 - UI
- [ ] A "Run in tmux" choice when a session is created (`StartAgentDialog`; if the dialog is in
      `UI.wxcp`, change it through `wxcgen` per `CLAUDE.md`, check `git status` first), disabled
      with the reason on Windows local. Default from a new preference (`UiPrefs` / settings).
- [ ] Mark tmux sessions: a small badge on the tab and "tmux: kennel-xxxx" in the status bar
      (`PublishStatus()`); the context menu of the tab: Detach, Kill tmux session.
- [ ] Settings: tmux **on by default** (a checkbox), and **two editable lists of tmux commands**:
      one for agent sessions (default: the four settings) and one for terminals (default in
      "Decisions", 1), with "Reset to default". The probe picks `all` / `on` for passthrough
      from the host's tmux version, so the stored text must not hard-code it (or the command is
      checked on the host and refused ones are reported).

### Phase 5 - Find sessions that are still running
- [ ] On start, each restored tmux session runs the attach-or-create command (see "Lifecycle,
      Kennel starts"): attach if alive, else new with `--continue`. One `new-session -A`, no
      pre-check. `ListSessions()` is for the "Attach to a running session" dialog and for the
      `HasSession()` question after a terminal ends, not for deciding at startup.
- [ ] "Attach to a running tmux session..." dialog: lists `kennel-*` sessions of a host that no
      persisted session owns (for example after `workspace.json` was lost) and adopts one.
- [ ] Ssh failures at startup must not block the window: do the probing on worker threads and
      show the tab as "Connecting".

### Phase 6 - Docs and release
- [ ] `README.md` (a section: what it does, where it works, the detach key, scrolling) and the
      release notes.
- [ ] Manual checks (no test suite): remote, WSL, Linux; close Kennel and reopen; kill the ssh
      connection; reboot the remote host (resume path); two Kennel windows on one host; a
      folder or prompt with quotes and non-ASCII; a very long running agent; copy / paste and
      OSC 52; the activity icon on the tab; a host without tmux; an old tmux.
- [ ] Build (`./build.sh`) + `clang-format` before each commit; commits signed off.

## Decisions (answered by the user, 2026-11-13)
1. **Scope: both** agent sessions and plain terminals. The four settings (`mouse on`,
   `mode-keys vi`, `set-clipboard on`, `allow-passthrough all`) are for the **agent sessions**.
   **Terminals use other settings, and they are configurable** (a preference for each kind: the
   list of tmux commands, with the defaults below). Plain terminals do not need the shim by
   default (no agent reports a status); the settings go on the `new-session` line.
   - Agent defaults: the four above. Terminal defaults (proposed, to confirm): `set -g mouse on;
     setw -g mode-keys vi; set -g set-clipboard on`, no `allow-passthrough`.
   - **One server cannot hold two sets of `-g` settings** (the last session started would win,
     and `set-clipboard` is a server option). So each kind gets its own tmux server: agents
     `tmux -L kennel`, terminals `tmux -L kennel-term`. (Alternative if one server is wanted:
     scope the options to the session / window with `-t`, which cannot do `set-clipboard`.)
2. **Default: tmux on**, wherever the probe says it is available (tmux >= 3.3 on the host).
   Not available (Windows local, no tmux, too old): off, with the reason. Still a choice per
   session when it is created.
3. **Windows local: fine for v1** ("tmux needs WSL or a remote host on Windows").
4. **Closing a tab and archive** (the user: "should we close it? we do have archive"): the
   dialog does **not** get a third choice. **Close = stop**, as today (the tmux session is
   killed with the agent). **Archive = keep running**: the tmux session is detached and stays,
   and restoring the archived session re-attaches it. Closing Kennel itself only detaches.
   *Interpretation, to confirm.*
5. **Isolated servers** (`-L kennel`, `-L kennel-term`): yes. To look at them yourself:
   `tmux -L kennel ls`.
6. **Scrolling: `mouse on`** (tmux scrollback).
7. **Restart: re-attach is enough.** No separate "Restart agent" (kill the tmux session and
   start fresh with resume) in v1.
8. **A second client on a live session: share it** (`new-session -A`, no `-d`).

## Open questions (for the user)
- Terminal defaults in decision 1 (the three settings, no passthrough, no shim): right?
- Decision 4 (close = stop, archive = keep running): right?

## Findings

### 2026-11-13: what tmux forwards (tmux 3.4, Ubuntu 24.04 in WSL, no Kennel)
Method: a tmux client on a pty that plays the outer terminal, a script in the pane, and a look
at the bytes the client writes out. Own server (`-L`), `-f /dev/null`. Script:
`tmux_osc_test.py` (in the session scratchpad, not kept).

| What the program sends | passthrough off | passthrough on |
|---|---|---|
| OSC 7501, 9;4, 7 sent plain | dropped | dropped |
| OSC 7501, 9;4, 7, 52, 0 sent in the DCS wrapper | dropped | **forwarded** |
| OSC 52 / title sent plain, with `set-clipboard on` / `set-titles on` | forwarded (tmux makes its own) | same |

- Confirmed: unknown OSC are dropped. `allow-passthrough on` alone does not help: the **sender
  must wrap** (`ESC P tmux ; <ESC doubled> ESC \`). Wrapping with passthrough off is dropped too.
- With passthrough on, the wrapped OSC 7501, 9;4 and 7 arrive at the outer terminal as plain
  sequences, so Kennel's emulator needs no change.
- **The support query works if wrapped.** The wrapped `OSC 7501;?` reached the outer terminal,
  and the reply the outer terminal wrote came back **into the pane** as input (tmux hands what
  it can not parse to the pane). So a program that wraps its query and its reports when `$TMUX`
  is set would work with Kennel as it is. Not tested: that tmux delays it by `escape-time`
  (set it to 0 in the options), or how it is when the reply arrives in several pieces.
- OSC 52 (copy) and the title need no wrapping: `set-clipboard on` and `set-titles on`
  are enough.

### 2026-11-13: Claude Code in tmux (the user's test, and the binary)
- The user ran `claude` inside tmux in a Kennel session: **the tab shows nothing**.
- Reason, read from the strings of `claude.exe` 2.1.323 (minified JS, so this is a reading, not
  a trace): Claude Code probes with `OSC 7501 ; ?` at start ("probe: no reply to OSC 7501 ; ?")
  and only sends reports when the terminal answers. The query (`Xhi()`) and the reports are
  made by `yE(...)`, which writes the **plain** sequence. It has a wrapper (`$j()`: with
  `$TMUX` set it makes `ESC P tmux ; <ESC doubled> ESC \`) but the 7501 query and reports do
  not go through it. tmux drops the plain query (table above), so no reply ever comes, and
  Claude Code never reports. A setting on tmux can not change that.
- Searched the web: no sign that tmux itself understands OSC 7501 (or 9;4). The "support" is the
  DCS passthrough, which needs the sender to wrap. Claude Code issues describe the same for
  OSC 9;4 inside tmux (claude code changelog 2.1.78 and a later bug report disagree on whether
  that is fixed; not checked).
- So the activity icon in tmux needs the **shim** (option 2 of the OSC section), now written and
  released as `tmux-shim` (pin v1.1.1, see "The shim"). What it must do,
  now known exactly: (1) read the agent's output; (2) answer `ESC ] 7501 ; ?` **itself**, by
  writing `ESC ] 7501 ; ? : states=idle,working,done,blocked,error : kinds=permission,question,auth ESC \`
  to the agent (so the probe succeeds, without a round trip through tmux and Kennel);
  (3) re-emit each `ESC ] 7501 ; ...` report, and OSC 9;4 and OSC 7, wrapped for tmux (and run
  with `allow-passthrough all`). Everything else is copied unchanged.
- Kiro CLI and Codex: not checked yet (Kiro is where Kennel's support came from).
- Scrolling model (mouse on or off) and the WSL idle shutdown: not tested yet.
