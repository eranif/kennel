# Building Kennel

This document covers building Kennel from source and the conventions to follow when
contributing. For what Kennel is and how to use it, see [`README.md`](README.md).

## macOS

### Prerequisites

1. Install **Homebrew** – <https://brew.sh/>
2. Run `brew install cmake git`
3. Download the latest Xcode from the App Store
4. In Xcode, open **Preferences → Downloads** and install the Command Line Tools (adds `clang`/`clang++` to `/usr/bin`)

### Build

```bash
mkdir -p $HOME/devl
cd $HOME/devl
git clone https://github.com/eranif/kennel.git
cd kennel
./build.sh
```

### Run It

```bash
open $HOME/devl/kennel/.build-release/kennel.app
```

## Windows

### Prerequisites

1. Install **MSYS2** from <https://www.msys2.org/>
2. Open the MSYS2 terminal and run the following command to install build dependencies:

```bash
pacman -Sy git                                  \
           mingw-w64-clang-x86_64-toolchain     \
           mingw-w64-clang-x86_64-python3       \
           mingw-w64-clang-x86_64-cmake         \
           mingw-w64-clang-x86_64-libffi        \
           mingw-w64-clang-x86_64-jq            \
           mingw-w64-clang-x86_64-libxml2       \
           mingw-w64-clang-x86_64-llvm-openmp   \
           mingw-w64-clang-x86_64-ntldd         \
           mingw-w64-clang-x86_64-zlib          \
           unzip
```

### Build

```bash
mkdir -p $HOME/devl
cd $HOME/devl
git clone https://github.com/eranif/kennel.git
cd kennel
./build.sh
```

### Run It

```bash
$HOME/devl/kennel/.build-release/installer/bin/kennel.exe
```

## Project Structure

```
kennel/
├── src/
│   ├── app/                  # UI layer (wxWidgets)
│   └── core/                 # Core logic (GUI-free; wxBase types only)
├── assets/                   # SVG icons, terminal themes, app icon
├── screenshots/              # Images used by README.md
├── CMakeLists.txt
└── LICENSE                   # BSD 3-Clause
```

## Architecture

Kennel is layered:

1. **UI layer** (`src/app/`) — wxWidgets. `MainFrame` hosts the window, menu, and
   toolbar; `MainView` owns the group/session sidebar tree and the terminal area;
   `SessionPage` is a `wxPanel` that embeds a `wxTerminalViewCtrl` and launches the
   agent's child process.
2. **Core layer** (`src/core/`) — GUI-free business logic: paths, config, the agent
   registry, the workspace/session registry, JSON persistence, the output detector, and
   logging. It uses only wxBase types (`wxString`, `wxFileName`) so it stays portable and
   testable.
3. **Terminal emulation** — the `wxTerminalEmulator` library (fetched automatically at
   build time via CMake FetchContent) provides PTY spawning, ANSI rendering, and
   scrollback.

### Design Principles

- **No C++ exceptions.** Errors flow through `Status` / `StatusOr<T>` (absl-style).
- **GUI-free core.** `kennel_core` avoids GUI dependencies so session/agent logic can be
  reasoned about and tested independently of the UI.
- **Config-driven agents.** Agents are JSON definitions, not hardcoded — adding one needs
  no code change.
- **Fail-safe persistence.** Corrupt `config.json` / `workspace.json` self-recover: the
  bad file is backed up (`*.bak-<timestamp>`) and safe defaults are used, so the app
  always launches.

## Code Conventions

### Naming & Style

- **Methods:** PascalCase (`LogsDir()`, `Load()`, `OnSessionExited()`).
- **Formatting:** all sources MUST be `clang-format`-clean before committing. The root
  `.clang-format` uses `IndentWidth: 2`.
  Format only Kennel's own files under `src/` and skip the vendored `src/core/json.hpp`:

```bash
find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) ! -name 'json.hpp' \
  | xargs clang-format -i
```

### Data Types

- **Strings:** prefer `wxString` in interfaces and storage; convert at the JSON/std
  boundary with `s.ToStdString(wxConvUTF8)` and `wxString::FromUTF8(s)`.
- **Paths:** prefer `wxFileName`.
- **JSON:** `nlohmann/json`, vendored as a single header at `src/core/json.hpp`. Keep the
  include confined to `.cpp` files — do **not** put it in public headers.

### Error Handling

```cpp
#include "core/Status.h"

// Operations returning nothing:
if (Status st = DoSomething(); !st.ok()) {
  KLOG_ERROR() << "Failed: " << st.message();
  return st;
}

// Operations returning a value:
StatusOr<int> count = CountItems();
if (!count.ok()) {
  return count.status();
}
int n = count.value();
```

### Logging

Use the streaming logger, **not** `wxLog*`:

```cpp
#include "core/Logger.h"

KLOG_DEBUG() << "Session created: " << name;
KLOG_INFO()  << "Launched agent " << agentName;
KLOG_WARN()  << "Config not found, using defaults";
KLOG_ERROR() << "Failed to write workspace: " << st.message();
```

Output goes to `~/.kennel/logs/kennel.log`; the newline is appended automatically.

## Contributing

1. **Build and verify** with CMake before pushing.
2. **Format** your changes with `clang-format` (command above).
3. **No C++ exceptions** — use `Status` / `StatusOr<T>`.
4. Keep `kennel_core` GUI-free; session/UI logic belongs in the app layer.

[1]: https://brew.sh/
[2]: https://wxwidgets.org/downloads/
[3]: https://www.wxwidgets.org/downloads
