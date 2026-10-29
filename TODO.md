## TODO tasks before version 2.0.0 is released.

- [x] Update CI to include installer for Windows (Similar to CodeLite's MSYS2 CI installer stage)
      Workflow added (`.github/workflows/msys2.yml`): check its first run on GitHub, then tick this. Later: attach
      the installer to the GitHub release on `v*` tags; add Windows on ARM (clangarm64, `windows-11-arm`) back:
      CMake hardcodes `libcrypto-3-x64.dll` and the `.iss` allows x64 only
- [x] Gather files in their view (FlatView) similar to how we group terminals.
- [x] Files should be saved as part of the session
- [x] Add goto line to file editor
- [ ] Design and implement a file explorer view (maybe we can import clTreeCtrlPanel)
- [ ] Design and implement a git view.
- [ ] Settings dialog:
  - [ ] Review timeout.
  - [ ] Review turns.
- [ ] Add Archive feature
- [ ] Add filter on top of the flat view
- [ ] Make the flat view "multi-select".
- [ ] Add Size column to the flat view (useful for files)
- [x] Notebook: closing a tab, selects the first one. We should select the next one from the MRU.

