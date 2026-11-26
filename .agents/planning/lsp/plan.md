# Plan: LSP support in Kennel

Progress: `[x]` done, `[ ]` open. Tick a box when its commit is pushed.
Status as of 2026-11-26. Phase 1 is started: `LspServer`, `JsonRpcFramer` and a skeleton of
`LspClient` are pushed (`src/core/lsp/`). `Status` has reason codes (`StatusCode`, committed, not
pushed). The flow of `LspManager::GetServerFor()` is decided (see "The flow of a file"). Nothing
has been run against a real server yet.

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
exceptions (`Status`/`StatusOr`), `src/core/` GUI-free, wx events (or `CallAfter()`) instead of
`std::function`, **never a detached thread** (a thread belongs to its object and is joined in the
destructor), helpers are methods of the object (not free functions, not `XxxOf` / `XxxFor`),
`KLOG_*`, simple single-purpose classes, no mirrored state (ask the runtime), build +
clang-format before every commit, commits signed off (`-s`), no Co-Authored-By, no push without
approval.

## What is already there
- `Process::StartInteractive` + `Write` + `Stop`: a child with stdin open and an output callback.
  This is how **every** server runs (see `LspServer`). It delivers chunks, not messages (framing
  is `JsonRpcFramer`'s). It has no "the process ended" notification, and `Write()` waits when the
  pipe is full.
- `wsl.exe -d <distro> --exec <cmd>` through `Process`: a WSL server is a local child of
  wsl.exe, and the server sees Linux paths.
- `ssh <host> <cmd>` through `Process`: an ssh server is a local child of `ssh`.
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
- Request ids and a response table (the `Content-Length` framing is done: `JsonRpcFramer`).
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
1. `JsonRpcFramer` (done): `Feed(buffer)` takes what the server wrote (any thread); a thread of
   its own (owned, joined in the destructor, waits with a 10 ms `wait_for`) cuts it into
   `Content-Length: N\r\n\r\n<json>` messages, so a very long body does not stall the UI, and
   gives each one to `LspClient::OnParsed()` with `CallAfter()` (so it runs on the UI thread).
2. `LspServer` (done): one language server process, always a local `Process`
   (`Process::StartInteractive`). `LspServer(LspClient *client, TransportType, LspKind, target,
   user)`; `TransportType` is `kWsl` / `kLocal` / `kSsh`, `LspKind` is `kClangd` / `kGopls` /
   `kRustAnalyzer`. `Start()`, `Send(bytes)`, `Stop()`. The command is built for the type: the
   program itself, `wsl.exe -d <distro> --exec <program>`, or `ssh -T -o BatchMode=yes
   [user@]host '<program>'`. What the server writes to stdout and stderr is given to the client
   with `CallAfter(&LspClient::OnServerStdout / OnServerStderr, ...)`.
3. `LspClient` (a skeleton): owns one `LspServer` and one `JsonRpcFramer`; is a `wxEvtHandler`
   (for `CallAfter()`). Done: `Send(body)` (adds the header), `OnServerStdout()` (feeds the
   framer), `OnServerStderr()` (logs it), `OnParsed()` (a stub). To do: `Request(method,
   params)` returning an id, the answers and the server notifications as events, `initialize` /
   `initialized` / `shutdown` / `exit`, request timeouts, `$/cancelRequest`, a crash (restart
   with a limit), the capabilities of the server (including the semantic tokens legend). Knows
   nothing of editors.
4. `LspManager`: the single owner of the servers (`LspManager::Get()`), and the only thing a
   page talks to. A server is identified by `{ Account, WorkingDirectory, language }` (the
   `m_rootDir` of the `Account`; a server speaks one language), at most one per key. The
   working directory is the one of a session (see below). Reads `LanguageServerConfig`. The one
   entry point is `StatusOr<std::shared_ptr<LspClient>> GetServerFor(key)`: it returns the
   client when the server is ready, and otherwise it starts what is missing (check, install,
   launch, handshake) by itself and returns an error `Status` whose code says why (see "The flow
   of a file"). There is no `Start()`. The answers of the other requests
   (`manager->SemanticTokens(...)` and the like) come back as events.
5. `LanguageServerConfig`: see below.
6. `LspUri`: `file:///` URIs (Windows drive letters, percent-encoding, Linux paths for WSL).
7. `LspPositionConverter`: UTF-8 (STC bytes) <-> UTF-16 (LSP) per line, cached per document
   version. A fallback when the server accepts `utf-8` (open question 6).

`src/app/lsp/` (UI):
8. `LspDocument` (one per `FilePage`): talks to the manager by events only. `didOpen` on load,
   `didChange` (debounced ~250 ms) on STC modify, `didSave`, `didClose`. Applies results to the
   `Editor`.
9. `LspSemanticHighlighter`: semantic tokens -> STC styles / indicators (used by `LspDocument`).
10. `LspSymbolsDlg`: the popup of the document outline and of the workspace symbols (like Go to
    Anything).
11. `LspReferencesView`: the find-references results, in the "LSP" tab of the side bar. Shares a
    base with `FindResultsView` if the two overlap enough.
12. `LspLocationHistory`: back / forward after a jump.
13. `LspServersPage`: the Settings page (server list, enable/disable, restart, log level).

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
the distro (`wsl.exe -d --exec`, Linux paths, no `\\wsl$` URIs); ssh file -> server on the host
(`ssh`).
The working directory of the server is **the one of a session**: a session page already knows
its account and its working directory (`SessionPage::GetAccount()` / `m_session.workingDir`).
A file does not keep it: when the file is opened, the open sessions are asked and the one that
fits best wins (the same account, and a working directory that holds the file; the deepest
when several do). Nothing is stored in the file page or in the workspace for this. There is no
search for markers (`.git`, `Cargo.toml`, ...) any more.

Never install anything without the user: a missing server is offered ("Download and Install
clangd?" through `RemoteInstaller`, the same check-only / ask / install steps and the same
message box as ripgrep in `FindInFilesDlg`). A "Language Servers" menu/status item shows what
runs and lets the user stop it.

### The flow of a file
1. A file opens. The `FilePage` works out the language (from the extension), the account
   (`GetAccount()`) and the working directory (the best fitting open session). No language or no
   fitting session: nothing more happens.
2. The page calls `LspManager::Get().GetServerFor(key)`. The answer is the client, or a `Status`:
   - the entry is **Ready**: the client (`shared_ptr<LspClient>`);
   - no entry yet: the entry is created in **Checking**, the work starts in the background, and
     the answer is `kUnavailable`;
   - **Checking, AwaitingConsent, Installing, Initializing**: `kUnavailable` (try again after
     the event of step 7);
   - **Declined**: `kPermissionDenied`;
   - no server for this language, or no build of it for this machine: `kNotAvailable`;
   - **Failed**: another code, with the message.
3. **Check**: `RemoteInstaller` in `Mode::kCheckOnly` for the `ToolDescription` of the server, on
   the account. An error -> **Failed**. Installed -> step 5. `NeedsInstall()` -> **AwaitingConsent**
   and `wxEVT_LSP_INSTALL_NEEDED` (key, tool).
4. **Ask**: `MainFrame` handles it with the message box of ripgrep and calls
   `manager->Install(key)` (-> **Installing**, `RemoteInstaller` in `Mode::kInstall`, progress
   forwarded as events, then step 5 or **Failed**) or `manager->Decline(key)` (-> **Declined**).
5. **Launch**: the manager makes the `LspClient` (type from the account, kind from the language)
   and starts it. -> **Initializing**.
6. **Handshake**, once per server: `initialize` (`rootUri`, client capabilities with
   `positionEncodings`), the response (the capabilities of the server are kept), `initialized`.
   -> **Ready**.
7. The manager sends `wxEVT_LSP_SERVER_READY` (key), or `wxEVT_LSP_SERVER_FAILED` (key,
   message). The page that got `kUnavailable` handles it, calls `GetServerFor(key)` again, gets
   the client and sends `didOpen` (then `didChange`, `didSave`, `didClose`). The manager keeps no
   list of documents and queues nothing for the page.
8. The server asks the client things: `window/workDoneProgress/create`,
   `client/registerCapability`, `workspace/configuration` must be answered, or clangd and
   rust-analyzer wait for ever.
9. The last document of a key closes: after a grace period `shutdown`, `exit`, `Stop()`. A crash
   -> **Failed** (restart with a limit).

`StatusCode` (`core/Status.h`, done): `kUnknown` (the default of `Status::Error(message)`),
`kUnavailable` (not ready, try again), `kPermissionDenied` (the user said no), `kNotAvailable`
(nothing to use, and waiting does not help).

## Phases

Read-only features first, in this order: document sync, semantic highlighting, go to definition,
document outline, workspace symbols, find references. Editing features come after (Phase 8).

### Phase 0 - Spike (decide before building)
- [ ] Run clangd by hand from Kennel code on a local file: `initialize`, `didOpen`,
      `textDocument/definition`, print the answer to `KLOG_INFO`. Throw-away, not merged.
      Goal: confirm framing, the UTF-16 offsets, and how much `Process` needs (the output
      callback gives raw chunks; stdin writes must not block the UI thread).
- [ ] Try the remote case with `ssh host clangd` as a local `Process` (it is what `LspServer`
      does for `kSsh`): needs the OpenSSH client and keys that work without a question.

### Phase 1 - Server and client (`src/core/lsp/`)
- [x] `JsonRpcFramer` (framing, on a thread of its own).
- [x] `LspServer` (local, WSL and ssh as local processes; the command is built per
      `TransportType`; the program per `LspKind`).
- [ ] `LspClient`: skeleton done (owns the server and the framer, `Send()`, the output of the
      server). To do: handshake, requests/responses, notifications as events, cancel,
      timeouts, shutdown, crash detection.
- [ ] Capabilities: send a minimal client capabilities object (including `positionEncodings`,
      semantic tokens); keep the server's, and ask them (`SupportsDefinition()` ...) before
      sending a request.
- [x] Files added to `CMakeLists.txt` (for the classes above).
- [x] `StatusCode` in `Status` (`kUnavailable`, `kPermissionDenied`, `kNotAvailable`, ...), for
      `GetServerFor()`.

### Phase 2 - Manager, config, document sync (feature 1: didOpen / didChange / didSave / didClose)
- [ ] `LanguageServerConfig` + defaults + persistence (`ConfigStore`), `LspUri`.
- [ ] `LspManager`: `GetServerFor(key)` and the flow of a file (entry states Checking,
      AwaitingConsent, Installing, Initializing, Ready, Declined, Failed; `RemoteInstaller` check
      / install; `Install(key)` / `Decline(key)`; launch and handshake; the events
      `wxEVT_LSP_INSTALL_NEEDED`, `wxEVT_LSP_SERVER_READY`, `wxEVT_LSP_SERVER_FAILED`), the
      working directory from the sessions, the grace-period stop, the restart limit. The handler
      of the install question in `MainFrame`.
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
1. **Remote transport**: decided, `ssh` runs as a local process, like WSL and local servers
   (`LspServer`); there is no libssh channel. It needs the OpenSSH client on this machine and
   keys that work without a question (`BatchMode=yes`).
2. **One server per what?** Decided: per account, working directory (of a session) and
   language. Still open: a file that **no open session fits** (a file from the file tree of
   another machine, a file restored at startup before its session exists): no server, or the
   folder of the file as its working directory?
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
8. **How does the client learn that the server started or ended?** The `LspServer` events
   (opened, closed) were dropped, and `Process` has no end notification. The client needs it
   for the handshake and for crash detection.
9. **`PATH` of the server on WSL and ssh**: `wsl.exe --exec` and a command over ssh do not read
   the profile, so `~/.local/bin`, `~/.cargo/bin` and the like are not on `PATH`.
10. **`Process` and the lifetime of the client**: the output callback runs on a thread of
    `Process`; if the client is destroyed while it runs, it calls a client that is gone. Needs a
    change in `Process` (or a guard); `Process::Write()` can also wait when the server does
    not read.
11. **Where the server is installed**: with the ripgrep approach the program is
    `$HOME/.kennel/bin/<server>`, but `LspServer` runs the bare name, and `wsl.exe --exec` does
    no `$HOME` expansion. `LspServer` needs the absolute path (`InstallEvent::GetPath()`).
12. **`PATH` first?** Phase 9 says to look for a server on `PATH` before installing; "like
    ripgrep" means `~/.kennel/bin` only. Not decided.
13. **Is "Declined" remembered**: in memory only (asked again at the next start), or kept in the
    config per account and language?
14. **One `RemoteInstaller` per key**: it runs one install at a time, so two files that open at
    once on different accounts need one installer each (or a queue).
