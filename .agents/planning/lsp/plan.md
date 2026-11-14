# Plan: LSP support in Kennel

Progress: `[x]` done, `[ ]` open. Tick a box when its commit is pushed.
Status as of 2026-11-14. Nothing is started.

## Context
Kennel opens files in `FilePage` (an `Editor`, a `wxStyledTextCtrl`) for this machine, a WSL
distribution and an ssh host. Today the editor has syntax colours and brace matching only. LSP
(Language Server Protocol) adds what a language server knows: diagnostics, go to definition,
find references, hover, completion, rename, document symbols.

Kennel is not an IDE: the main use is reading and reviewing what an AI agent wrote. So the order
is **read features first** (diagnostics, hover, go to definition, references, symbols), and the
editing features (completion, rename, formatting) come last, if at all.

Rules to keep (CLAUDE.md): never hand-edit `UI.cpp/UI.hpp` (use the `wxcrafter` skill), no
exceptions (`Status`/`StatusOr`), `src/core/` GUI-free, wx events instead of `std::function`,
`KLOG_*`, simple single-purpose classes, no mirrored state (ask the runtime), build +
clang-format before every commit, commits signed off (`-s`), no Co-Authored-By, no push without
approval.

## What is already there
- `Process::StartInteractive` + `Write` + `Stop`: a child with stdin open and an output callback.
  This is the transport for a **local** server. It delivers chunks, not messages (framing is ours).
- `wsl.exe -d <distro> -- <cmd>` through `Process`: a WSL server is a local child of wsl.exe, same
  transport, and the server sees Linux paths.
- `RemoteInstaller` + `ToolDescription`: installs a pinned tool into `~/.kennel/bin` of an account.
  Written to be reused for clangd (see its header).
- `Account` (this machine / WSL / ssh) and `FileLocation`; `FilePage::GetAccount()` and
  `GetAccountPath()` already give "whose file, which path" in the notation of the account.
- `Editor::GotoLine(line, column, length)` (select a range), `FileEvent` open-at-line, the
  `OutputView` (`wxAuiNotebook`, tabs by name) for a "Problems" tab, `SearchablePage`.
- `EventNotifier`, `Status`/`StatusOr`, `json.hpp` (only in `.cpp`).

## What is missing
- **A streaming ssh channel.** `SshClient::Execute` is one-shot (run, wait, get all output).
  A language server on a remote host needs a long-lived channel with stdin and stdout
  (libssh `ssh_channel`, `ssh_channel_read_nonblocking`). Phase 1 builds it, nothing else in
  Kennel needs it yet except the "remote search cannot stream" item of find-in-files.
- JSON-RPC framing (`Content-Length` headers), request ids, a response table.
- Position conversion: LSP counts **UTF-16 code units**, STC counts **bytes** (UTF-8). Needs a
  converter per line (`PositionFromLSP` / `PositionToLSP`), and it must be right for emoji
  (the find-in-files work hit the same class of bug).
- Document sync: STC modification events -> `didChange`.
- Everything above the transport: servers config, UI.

## Design

Layers, from the bottom. Each is one class with one job; the lower two are GUI-free `src/core/`.

1. `LspTransport` (core): `Start(account, argv, workingDir)`, `Send(bytes)`, `Stop()`. One
   implementation per account kind behind the same interface (local/WSL = `Process`,
   ssh = the streaming channel). Output arrives as `wxEVT_LSP_BYTES` (thread -> pending event).
2. `LspClient` (core): one running server. JSON-RPC: `Request(method, params)` returns an id,
   the answer comes as `LspEvent` (`wxEVT_LSP_RESPONSE`, request id, result or error),
   server notifications as `wxEVT_LSP_NOTIFICATION`. Handles `initialize` / `initialized` /
   `shutdown` / `exit`, request timeouts, `$/cancelRequest`, a crash (restart with a limit).
   Knows nothing of editors.
3. `LspManager` (core): the single owner of the servers. Key = (account, workspace root,
   language). `Acquire(file)` starts a server when none runs for that key, reference counted
   by the open files; the last file closed stops it after a grace period. Reads
   `LanguageServerConfig` (below). Posts `LspEvent` to the app through `EventNotifier`.
4. `LspDocument` (app, one per `FilePage`): talks to the manager by events only.
   `didOpen` on load, `didChange` (incremental, debounced ~250 ms) on STC modify, `didSave`,
   `didClose`. Owns the line/offset conversion. Applies results to the `Editor`.
5. Features in the editor/UI, each a small separate piece (see phases).

Servers config (`LanguageServerConfig`, core, persisted in the existing config store):
`{ language, fileExtensions, command, args, installable(ToolDescription?), initializationOptions }`
Defaults for clangd, gopls, rust-analyzer, pyright/pylsp, typescript-language-server,
lua-language-server. User can edit them (a section in Settings). Per-account override: the
command to run on a remote host may differ from the local one.

Where the server runs follows the file: a local file -> local server; WSL file -> server inside
the distro (`wsl.exe -d`, Linux paths, no `\\wsl$` URIs); ssh file -> server on the host.
The workspace root is the closest marker upward from the file (`.git`, `compile_commands.json`,
`Cargo.toml`, `go.mod`, `package.json`, `pyproject.toml`); the same "closest match" idea as the
find-in-files root, one function shared with it (extract it, do not copy it).

Never start anything without the user: a server is started only for a language that has a
configured and found command, and a missing server is offered once ("Install clangd on
<host>?" through `RemoteInstaller`, same message-box rule as ripgrep), remembered per
account + language. A "Language Servers" menu/status item shows what runs and lets the user stop it.

## Phases

### Phase 0 - Spike (decide before building)
- [ ] Run clangd by hand from Kennel code on a local file: `initialize`, `didOpen`,
      `textDocument/definition`, print the answer to `KLOG_INFO`. Throw-away, not merged.
      Goal: confirm framing, the UTF-16 offsets, and how much `Process` needs (the output
      callback gives raw chunks; stdin writes must not block the UI thread).
- [ ] Decide the ssh channel question (open question 1) with the spike result.

### Phase 1 - Transport and client (`src/core/`)
- [ ] `JsonRpc` framing: parse `Content-Length: N\r\n\r\n<json>` from a chunk stream (partial
      messages, several messages per chunk, UTF-8 bodies), write the same.
- [ ] `LspTransport` interface; local + WSL implementation on `Process::StartInteractive`.
- [ ] Streaming ssh channel in `SshClient` (or a new `SshChannel` class): open exec channel,
      write, read without blocking, close, keep-alive. Remote implementation of `LspTransport`.
- [ ] `LspClient`: handshake, requests/responses, notifications, cancel, timeouts, shutdown,
      crash detection. `LspEvent` in the style of `FindEvent` / `InstallEvent`.
- [ ] Capabilities: send a minimal client capabilities object; keep the server's, and
      ask them (`SupportsHover()` ...) before sending a request.
- [ ] Files added to `CMakeLists.txt`.

### Phase 2 - Manager, config, document sync
- [ ] `LanguageServerConfig` + defaults + persistence (`ConfigStore`), `LspManager`
      (key, ref count, grace-period stop, restart limit, root detection).
- [ ] `LspDocument` in `FilePage`: open/change/save/close, incremental change, debounce.
- [ ] Position converter with a UTF-8 <-> UTF-16 line table (cached per document version).
- [ ] Remote files: the URI is the path on the host; the content comes from the editor (we
      send it in `didOpen`), so the server never needs the file over a share.
- [ ] Unsaved/untitled/transient pages (git diff): never sent to a server.
- [ ] Status of each server (starting / ready / failed + the stderr tail) readable by the UI.

### Phase 3 - Read features (the useful part)
- [ ] Diagnostics: `publishDiagnostics` -> STC indicators (squiggles) + a margin marker +
      tooltip; a "Problems" tab in the `OutputView` listing them (click = `GotoLine`).
- [ ] Hover (`textDocument/hover`): call tip on mouse dwell, markdown stripped to text.
- [ ] Go to definition / declaration / type definition: Ctrl+click and F12 -> opens the file at
      the place via the `FileEvent` path (the target may be another file or another root).
      Several results -> a small popup list.
- [ ] Find references: results in the `OutputView` as a new tab reusing the layout of
      `FindResultsView` (file, line, text, click to open). A base class if the two share enough.
- [ ] Document symbols: an outline (a popup like Go to Anything, `Ctrl+Shift+O`), and
      workspace symbols in the Go to Anything bar if the server supports it.
- [ ] Back / forward navigation after a jump (a small history of locations).

### Phase 4 - Install servers
- [ ] `ToolDescription` for clangd (pinned URL + SHA-256 per OS/arch), install into
      `~/.kennel/bin` of the account with `RemoteInstaller`. Others (gopls, rust-analyzer) are
      downloaded the same way if they ship single binaries; npm-based ones are not installed
      by Kennel, only detected.
- [ ] Ask-before-install message box + remembered answer per account and language.
- [ ] Detect an existing server on `PATH` of the account first (`which` / `where`), use it
      instead of installing.

### Phase 5 - Editing features (optional, later)
- [ ] Completion popup (`textDocument/completion`, trigger characters, `resolve`), snippets
      reduced to plain text.
- [ ] Signature help.
- [ ] Rename symbol (`WorkspaceEdit` across files: applies to open pages and writes closed
      files; needs a preview list, so it is the riskiest feature; do last).
- [ ] Code actions / quick fix, formatting (`textDocument/formatting`).

### Phase 6 - Polish and release
- [ ] Settings page: server list, enable/disable per language, restart, log level.
- [ ] Server stderr and the JSON traffic into `~/.kennel/logs/` behind a debug switch.
- [ ] Manual checks (no test suite): local, WSL, ssh; a file with emoji and CJK (offsets);
      big file; the server crashing; the server missing; two roots at once; closing the last
      file stops the server; reconnect after the ssh connection drops; file changed on disk
      (`workspace/didChangeWatchedFiles`).
- [ ] Release notes and README mention.
- [ ] Build (`./build.sh`) + `clang-format` before each commit.

## Open questions
1. **Remote transport**: a new streaming exec channel on the existing libssh session
   (preferred: no extra program), or run `ssh.exe host server` as a local `Process` (works
   today with no new code, but needs the OpenSSH client and the user's ssh config/keys to work
   non-interactively, and a second connection)? The spike should try the second to see if it
   is good enough before writing the channel.
2. **One server per root, or one per file?** The plan says per (account, root, language), which
   is what the servers expect. Confirm that Kennel's file pages can name a root reliably.
3. **Scope of editing**: is Phase 5 wanted, or is Kennel read-only for LSP? It changes how much
   of `didChange`/sync correctness matters (a read-only use can resend the full text on save
   only).
4. **Which languages first?** Suggestion: clangd (the project itself is C++), then gopls and
   rust-analyzer. Say if another is more important.
5. **Language list in the editor**: `EditorLang` has 15 entries but no Go/Lua; the language
   for LSP is a separate key (by extension), so this is not blocked, but syntax colours for Go
   would be missing.
6. **Position encoding**: ask for `utf-8` through the `positionEncodings` capability (LSP 3.17)
   when the server supports it; it removes the UTF-16 conversion. Keep the converter as the
   fallback.
7. **Windows paths**: URIs for local Windows files (`file:///C:/...`, drive letter case,
   percent-encoding); WSL files must be sent with the Linux path to a server inside the distro.
