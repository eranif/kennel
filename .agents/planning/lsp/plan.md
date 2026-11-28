# Plan: LSP support in Kennel

Progress: `[x]` done, `[ ]` open. Tick a box when its commit is pushed.
Status as of 2026-11-28 (everything below marked done is pushed, up to `d816fe2`).
clangd works on this machine from start to end: the install question, launch, handshake,
`didOpen` / `didChange` / `didSave` / `didClose`, semantic highlights for C and C++, the watcher
with its backoff, the dead-server flow, and ending the servers that no page needs. The user saw
the highlights work. Verified by running (2026-11-28): local, WSL and ssh files (install,
handshake, highlights); a killed server is restarted and the pages register again; quitting
with C++ files open. Not verified by running: Save As and reload.

## Context
Kennel opens files in `FilePage` (an `Editor`, a `wxStyledTextCtrl`) for this machine, a WSL
distribution and an ssh host. Before LSP the editor had syntax colours and brace matching only.
LSP (Language Server Protocol) adds what a language server knows: semantic colours, go to
definition, find references, completion, rename, document symbols.

Kennel is not an IDE: the main use is reading and reviewing what an AI agent wrote. So the order
is **read features first** (semantic highlighting, go to definition, document outline, workspace
symbols, find references), and the editing features (code completion, rename) come later.

Rules to keep (CLAUDE.md, and what the user said in this work):
- **Ask, do not assume**: a class, a place, a name or a behaviour that the user did not give is the
  user's decision. Do what was named, nothing more.
- Never hand-edit `UI.cpp/UI.hpp` (use the `wxcrafter` skill); no exceptions (`Status` /
  `StatusOr`); `src/core/` GUI-free; **never a detached thread**; helpers are methods of the object
  (not free functions, not anonymous namespaces, not `XxxOf` / `XxxFor` unless the user named it);
  `KLOG_*`; no mirrored state (ask the runtime); build and `clang-format` before every commit;
  commits signed off (`-s`), no Co-Authored-By, no push without approval.
- **Events go through `EventNotifier`** (not to a window, not to `wxTheApp`), so that `src/core/`
  can handle them too. Event classes that the core needs live in `src/core/`.
- **No UI component is cached** by a core class: no `IEditor *`, no `weak_ptr` to a page. What the
  core needs it copies at the call, or fetches from the live application (`IManager`).

## What is already there
- `Process::StartInteractive` + `Write` + `Stop`: a child with stdin open and an output callback.
  This is how **every** server runs (see `LspServer`). It delivers chunks, not messages (framing
  is `JsonRpcFramer`'s). It has no "the process ended" notification: the manager polls
  `IsRunning()`.
- `wsl.exe -d <distro> --exec <cmd>` and `ssh <host> <cmd>` through `Process`: a WSL or ssh server
  is a local child of `wsl.exe` or `ssh`.
- `RemoteInstaller` + `ToolDescription`: installs a pinned tool into `~/.kennel/bin` of an account
  (used for clangd 23.1.0, binary only, limit 256 MB).
- `Account` (this machine / WSL / ssh), `FilePage::GetAccount()` and `GetAccountPath()`.
- `DialogAnswers` (`dialog-answers.json`) and the "Answers" page in Settings: a stored "No" to the
  install question, taken back by the user.
- Page events (`PageViewEvent`, `src/core/`): `wxEVT_PAGE_ADDED/REMOVED/RENAMED/MOVED/...`, for
  sessions, terminals and files, with the working directory, the page type, the path, the old file
  path and, for a file, the ID of its editor.
- `FileEvent` (`src/core/`): `wxEVT_FILE_SAVED` carries the editor ID; the save events
  (`SAVE_STARTED`, `SAVE_DONE`) go through the `EventNotifier`.

## Design

All LSP classes are in `src/core/lsp/` (GUI-free). What the core knows of the application is only
`IEditor` and `IManager` (both in `src/core/`); the application implements them.

**The interfaces (`src/core/`)**
- `IEditor` (implemented by `FilePage`): `GetAccount()`, `GetFilePath()` (in the notation of the
  account, a Linux path for WSL), `GetLanguageId()` (an `LspLanguage`), `GetText()`,
  `GetChangeCounter()` (counts inserts and deletes, never goes back), `GetEditorUID()`
  (`FilePageUID`, unique, never zero), `GetSessionAccount()` (the account of the session that
  holds the file, found once when the file is loaded), `NotifyLspTerminated()`. `GetServerKey()`
  is a method of the interface: it builds the `LspServerKey` from `GetSessionAccount()` and the
  language.
- `IManager` (implemented by `KennelManager` in `src/app/`): `GetActiveEditor()`,
  `GetAllEditors()`, `FindEditorByUID()`. `KennelApp` gives it to the core with
  `SetKennelManager()`; the core gets it with `GetKennelManager()` (`core/Globals.hpp`).

**The LSP classes (`src/core/lsp/`)**
1. `JsonRpcFramer`: cuts the output of the server into `Content-Length` messages on a thread of its
   own, and gives them to `LspClient::OnParsed()` on the UI thread.
2. `LspServer`: one server process (`kLocal` / `kWsl` / `kSsh`). `LspServerKey` is
   `{ Account (with the m_rootDir of a session), LspKind }`, with `GetKey()` (a readable text),
   `GetProgram()`, the install question and its answer key.
3. `LspClient`: owns one `LspServer` and one `JsonRpcFramer`; does the handshake (it asks for
   `utf-8` positions and semantic tokens) and **all the work of the editors**:
   - `NotifyOpened(editor)`: `didOpen` (or `didChange` for a reload); returns whether the server
     has the file. The version is the change counter.
   - `GetSemanticHighlightsFor(editor)`: `didChange` when the change counter moved, then
     `textDocument/semanticTokens/full`. Returns a `RequestID`. The answer is decoded
     (`LspSemanticTokens`) against the text of the live editor (fetched with `FindEditorByUID`),
     dropped if the text changed, and sent as `wxEVT_LSP_SEMANTIC_TOKENS`.
   - `CloseDocument(editor)`, `OnDocumentSaved(editor)` (`didClose`, `didSave`),
     `NotifyTerminated()` (tells every editor that has a file open on it).
   - It keeps only a map `FilePageUID -> change counter` (what the server has) and the requests
     that wait. No text, no editor.
4. `LspManager`: a **thin** owner of the servers (`LspManager::Get()`), one `Entry` per
   `LspServerKey`: a state machine (`kChecking`, `kAwaitingConsent`, `kInstalling`, `kInitializing`,
   `kReady`, `kRestarting`, `kDeclined`, `kFailed`) that owns the `RemoteInstaller` and the client.
   - `GetClientFor(key)` returns the client when ready and starts what is missing otherwise;
     `IsClientAvailableFor(editor)`; `NotifyOpened(editor)`; `NotifyClosed(editor)` (Save As);
     `GetSemanticHighlightsFor(editor)`. They find the server with `editor->GetServerKey()` and
     give the call to its client. **Nothing is queued or kept**: when the client is not ready the
     answer is "not available", and the editor asks again on `wxEVT_LSP_SERVER_READY`.
   - It listens to the page events: it keeps the list of open pages, ends the servers that no page
     needs (a session or terminal in the folder of the server, or a file inside it), and forwards
     a removed file (`didClose`) and a saved file (`didSave`) to the client.
   - One timer for all servers (1 s): a dead process is noticed (`IsRunning()`), the client tells
     the editors (`NotifyTerminated()`), and the server starts again with a backoff (1 s, doubling,
     60 s at most, 5 failures give up, a run of 60 s starts the count again).
   - `Shutdown()` (from `~MainFrame`) stops every server.
5. Events: `LspManagerEvent` (`wxEVT_LSP_INSTALL_NEEDED`, `wxEVT_LSP_SERVER_READY`,
   `wxEVT_LSP_SERVER_FAILED`) and `LspEvent` (the base of the answers: `RequestID`, account, path)
   with `LspSemanticTokensEvent` (`wxEVT_LSP_SEMANTIC_TOKENS`, the words by kind:
   `{ kClass => {...}, kFunction => {...}, kVariable => {...} }`). Later answers (completions,
   symbols, outline) derive from `LspEvent`.
6. `LspUri`, `LspLanguage`, `LspSemanticTokens` (token types -> words), `Clangd` (the
   `ToolDescription`).

**The editor side (`src/app/`)**
- `FilePage`: after the content is loaded (`LoadLocal`, `LoadRemote`, Save As) it resolves its
  session account once (`ResolveSessionAccount()`, the deepest session whose working directory
  holds the file), then `NotifyLanguageServer()`. Its state is `EditorLspState`: `kInit`,
  `kPendingReady` (no client yet: waits for `wxEVT_LSP_SERVER_READY`), `kRegistered`. It keeps the
  `RequestID`s that it waits for in a set and takes an answer only for its own ID. A page with no
  session looks again when a session is added (`wxEVT_PAGE_ADDED`).
- `Editor`: counts the changes (`GetChangeCounter()`, text inserts and deletes only) and colours
  the words: functions in keyword set 1 (`wxSTC_C_WORD2`), classes in set 3 (`wxSTC_C_GLOBALCLASS`),
  variables in a substyle of `wxSTC_C_IDENTIFIER` (as CodeLite does), re-applied when the theme
  rebuilds the lexer.

Where the server runs follows the file: a local file -> local server; WSL file -> server inside
the distro (Linux paths); ssh file -> server on the host. The working directory of the server is
**the one of a session** (no search for `.git` or other markers).

Never install anything without the user: a missing server is offered ("Install clangd on X?"
through `RemoteInstaller`, with a short title and the details in the extended message); a "No" is
stored in `DialogAnswers` and the user takes it back in Settings.

### The flow of a file
1. The file is loaded. `FilePage::ResolveSessionAccount()` finds the session that holds it. No
   session, or no language server for the file: the page waits for nothing (`kInit`), or looks
   again when a session is added.
2. `NotifyLanguageServer()`: `LspManager::IsClientAvailableFor()` starts the server when there is
   none (check, install question, launch, handshake). Not ready: the page is `kPendingReady`.
3. When `wxEVT_LSP_SERVER_READY` comes, a waiting page runs step 2 again.
4. Ready: `LspManager::NotifyOpened()` -> `LspClient::NotifyOpened()` sends `didOpen`; then
   `GetSemanticHighlightsFor()` sends the changes since (`didChange`) and the request.
5. The answer is decoded and sent as `wxEVT_LSP_SEMANTIC_TOKENS` with the `RequestID`; the page
   whose set holds the ID colours the words.
6. A save sends `didSave`; closing the page sends `didClose`; Save As closes the old path before
   the name changes and registers the new file as a file of its own.
7. The last page that needs a server closes: the manager ends it. The process dies: the clients
   tell the editors, which go back to waiting, and the restarted server brings the ready event.

`StatusCode` (`core/Status.h`): `kUnknown`, `kUnavailable` (not ready, on its way), `kPermissionDenied`
(the user said no), `kNotAvailable` (nothing to use, waiting does not help).

## Phases

### Phase 1 - Server, client, manager (`src/core/lsp/`)
- [x] `JsonRpcFramer`, `LspServer` (local, WSL, ssh as local processes).
- [x] `LspClient`: handshake, requests, responses, notifications, the files that are open.
- [x] `LspManager`: `GetClientFor()` and the flow (check, install, launch, handshake), the install
      question in `MainFrame`, the stored answer and the "Answers" page, the watcher with its
      backoff, ending the servers that no page needs, `Shutdown()`.
- [x] clangd `ToolDescription` (23.1.0), `StatusCode`, `LspUri`, the logging (`LSP | `, `--log-level`,
      `--stdout`).
- [x] The page events in `src/core/` (`PageViewEvent`), for sessions, terminals and files.

### Phase 2 - Document sync
- [x] `didOpen`, `didChange` (the whole text, from the change counter), `didSave`, `didClose`.
- [x] `IEditor`, `IManager`, `KennelManager`; the editor ID; `FileEvent` in `src/core/`.
- [x] A server that dies: `NotifyTerminated()`, the editors register again on the ready event.
- [ ] A look again when a session changes its working directory (`wxEVT_PAGE_MOVED`): not done.
- [ ] Incremental `didChange` (ranges): today the whole text is sent; matters for big files.
- [x] Remote files (ssh) and WSL files: tried, they work (the upload chunk of `SftpClient::PutFile`
      was raised from 32 KiB to 4 MiB: it was very slow).

### Phase 3 - Semantic highlighting (C and C++)
- [x] `textDocument/semanticTokens/full`, the legend, `utf-8` positions asked for (a server that
      counts in UTF-16 is not refused, see Decision 4), words by kind, keyword sets and a substyle in the C++ lexer.
- [ ] Refresh while the user types: today the editor asks once after the load (and after a reload
      or Save As). The user decided that; revisit when needed.
- [ ] Colours: mine for now (variables bright blue, classes cyan, functions blue); tune with the
      theme.
- [x] Other languages, built (not tried by running yet): go (gopls), rust (rust-analyzer), python
      (pyright), php (phpantom_lsp), each with its `LspKind`, `LspLanguage` and a mapping in
      `Editor::ApplySemanticWords()` (one method for each lexer): Go uses the C++ lexer like C++,
      Rust the keyword sets 3 to 5, Python three substyles of the identifiers, PHP two substyles of
      the keywords (`$variables` are coloured by the lexer). New `EditorLang::kGo` / `kPhp`
      (the PHP lexer is `wxSTC_LEX_PHPSCRIPT`; wx has no Go lexer).
- [ ] pyright has no semantic tokens at all (`languageServerBase.ts` has none; only Pylance does):
      Python files get the server but no semantic colours. gopls: not looked at what it offers.

### Phase 4 - Go to definition
- [ ] `textDocument/definition` (Ctrl+click, F12) through a new method of the manager and an
      `LspEvent` answer; several results -> a small popup list.
- [ ] Back / forward after a jump.

### Phase 5 - Document outline
- [ ] `textDocument/documentSymbol` in a dialog (through `UI.wxcp`): filter box, kinds, Enter goes to
      the line. The shortcut is to be decided (Ctrl+Shift+O is Open File).

### Phase 6 - Workspace symbols
- [ ] `workspace/symbol` in the same dialog (or as a mode of Go to Anything).

### Phase 7 - Find references
- [ ] `textDocument/references`: results in a new "LSP" tab of the side bar (through `UI.wxcp`),
      like `FindResultsView`.

### Phase 8 - Editing features (later)
- [ ] `GetCompletionsFor(editor)` (the second call of the manager that the user named): the popup,
      trigger characters, `resolve`.
- [ ] Rename (a `WorkspaceEdit` across files; the riskiest, last).

### Phase 9 - Servers and install
- [x] clangd: `ToolDescription`, the ask-before-install message box, the remembered "No".
- [x] A server that is installed on this machine is used as it is (`platform::Which()` in
      `Helpers.cpp`: the `PATH` first, then MSYS2 on Windows, the usual folders on macOS and
      Linux, Homebrew's llvm and Xcode's tools included). Nothing is asked, so the install question
      and a stored "No" apply only when none is found. For WSL and ssh the install flow is
      unchanged. Tried on Windows (MSYS2 clangd: the colours are right, the headers are found).
- A version check of the clangd that was found: none (accepted risk).
- [x] rust-analyzer (release 2026-11-23: `.gz` on Linux and macOS, `.zip` on Windows) and
      phpantom_lsp (1.1.0: `.tar.gz` / `.zip`) are installed like clangd (`RemoteInstaller` reads a
      plain `.gz` now, `ReadGzFile()`); pinned with their checksums. gopls (`go install
      golang.org/x/tools/gopls@latest`) and pyright (`npm install -g pyright`, started as
      `pyright-langserver --stdio`) have no download: they are only looked for (`platform::Which`).
- [x] The question has a "Remember my answer" checkbox (a `wxRichMessageDialog`), for the install
      question and for the "not found" notice of the servers that are not installed. Remembered:
      the answer is stored and used the next time (a "yes" installs without asking). Not
      remembered: a "no" holds until Kennel ends, and a "yes" is asked again next time.
- [ ] A server that is only looked for (gopls, pyright) is looked for on this machine only: for a
      WSL distribution or an ssh host it fails with a message. It needs a shell on the machine (a
      login shell: `command -v`, run on a worker thread).
- [ ] pyright on Windows is a `.cmd` file of npm: starting it is not tried yet.

### Phase 10 - Polish and release
- [ ] A Settings page for the servers (list, enable or disable, restart, log level).
- [ ] The traffic of the server in the log behind a debug switch.
- [ ] Manual checks (no test suite). Done: local, WSL, ssh; a server that crashes (killed, restarts);
      quitting with C++ files open. Open: emoji and CJK (offsets); a big file; a server that is
      missing; two roots at once; Save As and reload; closing the last file; a file changed on
      disk (`workspace/didChangeWatchedFiles`).
- [ ] Release notes and README mention.

### Not scheduled
Diagnostics (`publishDiagnostics` is ignored today: squiggles, a "Problems" list), hover, signature
help, code actions, formatting. Add a phase when one is wanted.

## Open items and known limits
- A file that no session holds gets no server until a session that holds it is added.
- A failed server (`kFailed`) is not tried again: a warning is logged (the user's decision). A
  declined one stays declined until the user takes the answer back.
- `LspSemanticTokens::GetWords` parses the capabilities JSON on every answer, on the UI thread;
  cache the legend in the client if a big file shows a pause.
- A page that is destroyed without its remove event leaves its entry in the map of the client
  until the server ends (harmless).
- The review folder `828df2cb-...` is gone; its `response-1.md` still describes the older design.

## Decisions (were open questions)
1. **Remote transport**: `ssh` runs as a local process, like WSL and local servers; needs the
   OpenSSH client and keys that work without a question (`BatchMode=yes`).
2. **One server per what**: per account, working directory (of a session) and language.
3. **Editing features** come after the read-only ones.
4. **Position encoding**: the client asks for `utf-8`; the offsets of the tokens are always taken
   as UTF-8 bytes, and a server that counts in UTF-16 is not refused (no converter: the offsets
   are right on a line that is ASCII; a word that is not valid text is skipped).
5. **A file with no session**: no server, until a session that holds it is added.
6. **Where the server is installed**: `$HOME/.kennel/bin/<server>`; the program path is built per
   machine (`LspServerKey::GetProgram()`).
7. **An installed server first**: on this machine `platform::Which()` is asked before anything;
   what it finds is used without a question and without a version check. The Kennel install
   (`~/.kennel/bin`) is for the machines where nothing is found, and for WSL and ssh. Reason:
   an installed clangd knows where the headers of its compiler are; the one that Kennel installs
   does not (no `--query-driver`), and without them the semantic tokens are missing.
8. **A "No" is remembered**: in `DialogAnswers`, per machine and server, shown in Settings.
9. **How the client learns that the server ended**: the manager polls `IsRunning()` once a second.
10. **UI components are not kept** by the core: a page is fetched from the live notebook by its
    editor ID (`IManager::FindEditorByUID()`).
