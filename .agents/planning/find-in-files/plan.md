# Plan: Find in Files

Progress: `[x]` done, `[ ]` open. Tick a box when its commit is pushed.
Status as of 2026-11-13.

## Context
Find in Files searches a folder of an account (this machine, a WSL distribution, a remote host)
with ripgrep, and shows the matches in a result view. The engine is committed (`22a84fa`); the
installer for rg and the whole UI are not.

Rules to keep (CLAUDE.md): never hand-edit `UI.cpp/UI.hpp` (change `UI.wxcp` and regenerate with
`wxcgen`, through the `wxcrafter` skill; check `git status` first), no exceptions
(`Status`/`StatusOr`), `src/core/` GUI-free, wx events instead of `std::function`, `KLOG_*`,
simple single-purpose classes, no mirrored state, build + clang-format before every commit,
commits signed off (`-s`), no Co-Authored-By, no push without approval.

## Phase 1 - Engine (done)
- [x] `FindInFiles`: `rg --json` on a worker thread, local / WSL / remote, `FindEvent`
      (`wxEVT_FIND_BATCH`, `wxEVT_FIND_DONE`), limit of `kMaxMatches` = 5000, `Cancel()`.
- [x] `Ripgrep` + `ToolDescription`: rg 15.2.0 (Linux x86_64 and aarch64 musl) pinned with SHA-256;
      `ripgrep::ExecutableOf(account)`, `ripgrep::LinuxInstallDir()` = `$HOME/.kennel/bin`.
- [x] `Sha256Hex()`, `ReadTarGzMember()`, `Process` on `CreateProcessW`, `Process::QuoteArgument`.
- [x] Release notes 2.1.0 mention the groundwork.

Known state: when `ExecutableOf()` is empty, `FindInFiles` ends with the error
"ripgrep is not installed". Nothing installs it yet.

## Phase 2 - RemoteInstaller (`src/core/`, not started)
A generic class: puts one tool of a `ToolDescription` into a target folder of an account. It must
work for any tool and any folder (clangd later), not only `rg` in `~/.kennel/bin`.

Steps for `Install(account, tool, targetDir)`:
1. Find the machine: `uname -s` / `uname -m` on the target (remote: `SshClient::Execute`;
   WSL: `wsl.exe -d <distro> -- uname ...`; Linux host: `wxGetOsDescription`-free, use `uname`).
   `ToolDescription::FindAsset()` -> no asset = an error that names the OS and CPU.
2. Check what is installed: run `<targetDir>/<binary> --version`; a start that equals
   `versionBanner` means done, nothing to do.
3. Download `asset.url` on this machine with `wxWebRequest` (like `UpdateChecker`), in memory.
   The download is done once on this machine, so a remote host needs no internet.
4. Check `Sha256Hex(data) == asset.sha256`; a mismatch is an error and nothing is written.
5. `asset.member` not empty: take it out with `ReadTarGzMember()`.
6. Upload to `<targetDir>/<binary>`:
   - remote: `SftpClient::PutFile` (creates the parents), then `chmod +x` with `SshClient::Execute`.
     Open point: `PutFile` is meant for small files and is not atomic; rg is ~6 MB. Check it
     handles that, otherwise write to `<binary>.tmp` and `mv`, so a failed upload never leaves a
     half file that looks installed.
   - WSL: write through `wsl::ToWindowsPath()` (the `\\wsl$` share), then `chmod +x` with `wsl.exe`.
   - Linux host: `wxFile` + `chmod`.
7. Verify: run `--version` again and compare with `versionBanner`.

Design:
- Events only: `InstallEvent` with `wxEVT_INSTALL_PROGRESS` (step, text) and `wxEVT_INSTALL_DONE`
  (`Status`), sent to the handler, like `FindEvent`. Runs on its own thread (SSH and `Execute`
  block); download itself is async and is waited for on that thread.
- Inputs: `Account`, `ToolDescription`, target folder. No knowledge of ripgrep inside.
- One install at a time per account + tool.

Done when: a remote host, a WSL distro and a Linux host without rg get it, and run again =
"already installed", and a wrong checksum writes nothing.

- [ ] `RemoteInstaller` + `InstallEvent` in `src/core/`
- [ ] Add the files to `CMakeLists.txt`

## Phase 3 - Make sure rg is there before a search (`src/core/`)
- [ ] `ripgrep::Ensure(account)` (or the caller in the app) runs `RemoteInstaller` when
      `ExecutableOf()` says there is no rg, or the version differs, and then the search starts.
- [ ] Windows and macOS: Kennel ships its own rg next to the program. Make sure the installer
      (InnoSetup) and the app bundle really copy `rg.exe` / `rg`. Without it the error says so.
- [ ] A search must never start an install by itself without the user knowing: the UI asks once
      ("Install ripgrep on <host>?") and remembers the answer per account.

## Phase 4 - UI (`src/app/`)
A new self-contained panel (like `FileExplorer` / `GitView` in the v2.0 design): its own
`.hpp/.cpp`, input through setters, output through wx events, no `MainFrame` / `MainView`
includes. `MainView` is the glue.

- [ ] `FindInFilesPanel` (a `wxPanel`): pattern, Regex / Match case / Whole word toggles, include
      and exclude globs, folder (defaults to the folder of the current session), Search / Stop.
      Reuses the look of `SearchBar`.
- [ ] A result view: matches grouped by file, line number + text with the match highlighted,
      count in the header, "truncated at 5000" notice, error text from `FindEvent::GetResult()`.
      Batches from `wxEVT_FIND_BATCH` are appended, not rebuilt.
- [ ] Double-click / Enter on a match -> event to `MainView`, which opens the file at that line
      (`FilePage`), for local, WSL and remote accounts.
- [ ] The widgets go into `UI.wxcp`; regenerate with `wxcgen --bitmaps-in-base-class-file`, then
      `clang-format`, and review `git diff`. If `wxcgen` is missing: tell the user what to add in
      wxCrafter. A handler `Bind`-ed in the subclass needs no change of the `.wxcp`.
- [ ] Menu item / toolbar button / shortcut (Ctrl+Shift+F); put the shortcut in the tooltip like
      the other buttons.
- [ ] Remember the last pattern, toggles and globs through `UiPrefs`, not in the panel class.
- [ ] Progress and the "installing ripgrep" state, from `InstallEvent`.

## Phase 5 - Polish and release
- [ ] Remote search cannot stream (`SshClient` has no streaming), so show "Searching..." and
      make `Cancel()` visible as "result dropped". Revisit if `SshClient` gets streaming.
- [ ] Respect `.gitignore` / hidden files: decide the defaults and add the toggles if wanted.
- [ ] Manual checks (there is no test suite): local folder, WSL, remote host, folder that does not
      exist, bad regex, non-ASCII path, a binary file, 5000+ matches, Cancel while it runs, rg
      missing on a remote host.
- [ ] Release notes: replace "groundwork, no user interface yet" with the feature.
- [ ] Build (`./build.sh`) + `clang-format` before each commit.

## Open questions
- Where does the panel live: a bottom pane, a tab of the left notebook, or a dialog? (The design
  rule keeps it movable, so this only changes the glue.)
- Does a WSL account install into the distro's `~/.kennel/bin`, or does it use the Windows `rg.exe`
  with `\\wsl$` paths? The engine currently uses the distro's own rg.
- Should the installer also support the macOS remote host (no asset is pinned for Darwin)?
