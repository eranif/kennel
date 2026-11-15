# Plan: LSP support in Kennel

Progress: `[x]` done, `[ ]` open. Tick a box when its commit is pushed.
Status as of 2026-11-16. Nothing is started.

## Context
Kennel opens files in `FilePage` (an `Editor`, a `wxStyledTextCtrl`) for this machine, a WSL
distribution and an ssh host. Today the editor has syntax colours and brace matching only. LSP
(Language Server Protocol) adds what a language server knows: diagnostics, go to definition,
find references, hover, completion, rename, document symbols.

Kennel is not an IDE: the main use is reading and reviewing what an AI agent wrote. So the order
is **read features first** (document sync, semantic highlighting, go to definition, document
outline, workspace symbols, find references), and the editing features (code completion, rename)
come later.

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
  side bar (`SideView`, a `wxAuiNotebook`: file explorer, git, activity, find results) which gets
  a new "LSP" tab for the results, `FindResultsView`, `SearchablePage`.
- `SshClient::ExecuteStreaming` (written for find-in-files): a remote command whose output
  arrives as it is produced. It is the read side only (no stdin).
- `EventNotifier`, `Status`/`StatusOr`, `json.hpp` (only in `.cpp`).

## What is missing
- **A bidirectional ssh channel.** `SshClient::ExecuteStreaming` streams the output of a
  command, but it has no stdin and ends with the command. A language server on a remote host
  needs a long-lived channel with stdin and stdout (libssh `ssh_channel`,
  `ssh_channel_read_nonblocking`). Phase 9 builds it (if needed), on what `ExecuteStreaming` already does
  for reading.
- JSON-RPC framing (`Content-Length` headers), request ids, a response table.
- Position conversion: LSP counts **UTF-16 code units**, STC counts **bytes** (UTF-8). Needs a
  converter per line (`PositionFromLSP` / `PositionToLSP`), and it must be right for emoji
  (the find-in-files work hit the same class of bug).
- Document sync: STC modification events -> `didChange`.
- Everything above the transport: servers config, UI.

## Design

Layers, from the bottom. Each is one class with one job. **All LSP classes live in a folder of
their own**: the GUI-free ones in `src/core/lsp/`, the ones that touch the UI in `src/app/lsp/`
(both folders go into `CMakeLists.txt`; includes are `core/lsp/Xxx.h`, `app/lsp/Xxx.hpp`).

`src/core/lsp/` (GUI-free):
1. `JsonRpc`: frames and unframes `Content-Length: N\r\n\r\n<json>` on a chunk stream (partial
   messages, several per chunk, UTF-8 bodies).
2. `LspTransport` (interface): `Start(account, argv, workingDir)`, `Send(bytes)`, `Stop()`.
   Output arrives as `wxEVT_LSP_BYTES` (thread -> pending event). One implementation per
   account kind:
   - `LspProcessTransport`: local and WSL, on `Process::StartInteractive`.
   - `LspSshTransport`: ssh, on a bidirectional channel (`SshChannel`). It may be dropped if
     `ssh.exe host server` as a local `Process` is good enough (open question 1).
3. `LspClient`: one running server. `Request(method, params)` returns an id, the answer comes as
   `LspEvent` (`wxEVT_LSP_RESPONSE`, request id, result or error), server notifications as
   `wxEVT_LSP_NOTIFICATION`. Handles `initialize` / `initialized` / `shutdown` / `exit`,
   request timeouts, `$/cancelRequest`, a crash (restart with a limit). Keeps the capabilities
   of the server (including the semantic tokens legend). Knows nothing of editors.
4. `LspEvent`: the events above, in the style of `FindEvent` / `InstallEvent`.
5. `LspManager`: the single owner of the servers. Key = (account, workspace root, language).
   `Acquire(file)` starts a server when none runs for that key, reference counted by the open
   files; the last file closed stops it after a grace period. Reads `LanguageServerConfig`.
   Posts `LspEvent` to the app through `EventNotifier`.
6. `LanguageServerConfig`: see below.
7. `LspUri`: `file:///` URIs (Windows drive letters, percent-encoding, Linux paths for WSL).
8. `LspPositionConverter`: UTF-8 (STC bytes) <-> UTF-16 (LSP) per line, cached per document
   version. A fallback when the server accepts `utf-8` (open question 6).

`src/app/lsp/` (UI):
9. `LspDocument` (one per `FilePage`): talks to the manager by events only. `didOpen` on load,
   `didChange` (debounced ~250 ms) on STC modify, `didSave`, `didClose`. Applies results to the
   `Editor`.
10. `LspSemanticHighlighter`: semantic tokens -> STC styles / indicators (used by `LspDocument`).
11. `LspSymbolsDlg`: the popup of the document outline and of the workspace symbols (like Go to
    Anything).
12. `LspReferencesView`: the find-references results, in the "LSP" tab of the side bar. Shares a
    base with `FindResultsView` if the two overlap enough.
13. `LspLocationHistory`: back / forward after a jump.
14. `LspServersPage`: the Settings page (server list, enable/disable, restart, log level).

Go to definition, and the call from the UI to the manager, are small methods of `Editor` /
`FilePage` / `LspDocument`, not classes. Dialogs and pages (`LspSymbolsDlg`, `LspServersPage`,
the "LSP" tab) are windows in `UI.wxcp`, through the `wxcrafter` skill, with their logic in the
"Inherited Class".

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

Read-only features first, in this order: document sync, semantic highlighting, go to definition,
document outline, workspace symbols, find references. Editing features come after (Phase 8).

### Phase 0 - Spike (decide before building)
- [ ] Run clangd by hand from Kennel code on a local file: `initialize`, `didOpen`,
      `textDocument/definition`, print the answer to `KLOG_INFO`. Throw-away, not merged.
      Goal: confirm framing, the UTF-16 offsets, and how much `Process` needs (the output
      callback gives raw chunks; stdin writes must not block the UI thread).
- [ ] Try the remote case with `ssh.exe host clangd` as a local `Process` and decide the ssh
      channel question (open question 1) with the result.

### Phase 1 - Transport and client (`src/core/lsp/`)
- [ ] `JsonRpc` framing.
- [ ] `LspTransport` interface; `LspProcessTransport` (local + WSL).
- [ ] `LspClient`: handshake, requests/responses, notifications, cancel, timeouts, shutdown,
      crash detection. `LspEvent`.
- [ ] Capabilities: send a minimal client capabilities object (including `positionEncodings`,
      semantic tokens); keep the server's, and ask them (`SupportsDefinition()` ...) before
      sending a request.
- [ ] Files added to `CMakeLists.txt`.

### Phase 2 - Manager, config, document sync (feature 1: didOpen / didChange / didSave / didClose)
- [ ] `LanguageServerConfig` + defaults + persistence (`ConfigStore`), `LspManager`
      (key, ref count, grace-period stop, restart limit, root detection), `LspUri`.
- [ ] `LspDocument` in `FilePage` (`src/app/lsp/`): `didOpen` on load, `didChange` on STC modify
      (debounced; a full-text send is fine for a start, incremental after), `didSave`,
      `didClose`.
- [ ] `LspPositionConverter` with a UTF-8 <-> UTF-16 line table (cached per document version).
- [ ] Remote files: the URI is the path on the host; the content comes from the editor (we
      send it in `didOpen`), so the server never needs the file over a share.
- [ ] Unsaved/untitled/transient pages (git diff): never sent to a server.
- [ ] Status of each server (starting / ready / failed + the stderr tail) readable by the UI.

### Phase 3 - Semantic highlighting (feature 2)
- [ ] `LspSemanticHighlighter`: `textDocument/semanticTokens/full` (and `/full/delta` when the
      server has it), the legend from the server capabilities, tokens (relative line / column,
      UTF-16) -> STC styles or indicators on top of the lexer colours. Re-requested after a
      `didChange` (debounced), the old tokens are dropped when the version changes.
- [ ] Token types and modifiers -> the colours of the active theme (`ThemeManager`).
- [ ] Not used for a file whose server does not offer it; a setting to turn it off.

### Phase 4 - Go to definition (feature 3)
- [ ] `textDocument/definition` (and declaration / type definition): Ctrl+click and F12 -> opens
      the file at the place via the `FileEvent` path (the target may be another file or another
      root, on the same account). Several results -> a small popup list.
- [ ] `LspLocationHistory`: back / forward after a jump.

### Phase 5 - Document outline (feature 4)
- [ ] `textDocument/documentSymbol` (hierarchical `DocumentSymbol` and flat `SymbolInformation`)
      in `LspSymbolsDlg`: filter box, kind icons, Enter = `GotoLine`. The shortcut is to be
      decided (Ctrl+Shift+O is Open File).

### Phase 6 - Workspace symbols (feature 5)
- [ ] `workspace/symbol` in the same `LspSymbolsDlg` (or as a mode of Go to Anything), debounced
      query as the user types, results from the server of the current file's root.

### Phase 7 - Find references (feature 6)
- [ ] `textDocument/references`: results in the new "LSP" tab of the side bar (a page of the
      side-bar notebook, added to `UI.wxcp` through the `wxcrafter` skill), reusing the layout of
      `FindResultsView` (file, line, text, click to open). `LspReferencesView`, with a base class
      if the two share enough.

### Phase 8 - Editing features (later)
- [ ] Code completion popup (`textDocument/completion`, trigger characters, `resolve`),
      snippets reduced to plain text.
- [ ] Rename symbol (`WorkspaceEdit` across files: applies to open pages and writes closed
      files; needs a preview list, so it is the riskiest feature; do last).

### Phase 9 - Install servers and remote
- [ ] `ToolDescription` for clangd (pinned URL + SHA-256 per OS/arch), install into
      `~/.kennel/bin` of the account with `RemoteInstaller`. Others (gopls, rust-analyzer) are
      downloaded the same way if they ship single binaries; npm-based ones are not installed
      by Kennel, only detected.
- [ ] Ask-before-install message box + remembered answer per account and language.
- [ ] Detect an existing server on `PATH` of the account first (`which` / `where`), use it
      instead of installing.
- [ ] `LspSshTransport` + `SshChannel` (bidirectional exec channel, extending
      `ExecuteStreaming`), if Phase 0 says `ssh.exe` is not good enough.

### Phase 10 - Polish and release
- [ ] `LspServersPage`: server list, enable/disable per language, restart, log level.
- [ ] Server stderr and the JSON traffic into `~/.kennel/logs/` behind a debug switch.
- [ ] Manual checks (no test suite): local, WSL, ssh; a file with emoji and CJK (offsets);
      big file; the server crashing; the server missing; two roots at once; closing the last
      file stops the server; reconnect after the ssh connection drops; file changed on disk
      (`workspace/didChangeWatchedFiles`).
- [ ] Release notes and README mention.
- [ ] Build (`./build.sh`) + `clang-format` before each commit.

### Not scheduled
Diagnostics (`publishDiagnostics`: squiggles, margin marker, a "Problems" list), hover, signature
help, code actions / quick fix, formatting. They are not in the list of features to build; add a
phase when one is wanted.

## Open questions
1. **Remote transport**: a new streaming exec channel on the existing libssh session
   (preferred: no extra program), or run `ssh.exe host server` as a local `Process` (works
   today with no new code, but needs the OpenSSH client and the user's ssh config/keys to work
   non-interactively, and a second connection)? The spike should try the second to see if it
   is good enough before writing the channel.
2. **One server per root, or one per file?** The plan says per (account, root, language), which
   is what the servers expect. Confirm that Kennel's file pages can name a root reliably.
3. **Scope of editing**: decided, the editing features (Phase 8: code completion, rename) come
   after the read-only ones. Sync still has to be correct from Phase 2, because semantic
   tokens and every request depend on the server seeing the same text as the editor.
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
