## TODO tasks before version 2.0.0 is released.

- [x] Update CI to include installer for Windows (Similar to CodeLite's MSYS2 CI installer stage)
      Workflow added (`.github/workflows/msys2.yml`): check its first run on GitHub, then tick this. Later: attach
      the installer to the GitHub release on `v*` tags; add Windows on ARM (clangarm64, `windows-11-arm`) back:
      CMake hardcodes `libcrypto-3-x64.dll` and the `.iss` allows x64 only
- [x] Files should be saved as part of the session
- [x] Add goto line to file editor
- [x] Design and implement a file explorer view (maybe we can import clTreeCtrlPanel)
- [ ] Design and implement a git view.
- [ ] Settings dialog:
  - [ ] Review timeout.
  - [ ] Review turns.
- [ ] Add Archive feature
- [x] Notebook: closing a tab, selects the first one. We should select the next one from the MRU.
- [ ] Update the `File Browser` with the following: add `Refresh` context menu. `Open with Default App`.
- [ ] Make `Cobalt2` the default theme
- [ ] Make the tree-viwe multi-select.
- [ ] BUG: Tab Spinner is NOT spinning.
- [ ] Tab icons are too small.
- [ ] Allow the user to change the tab position.
- [ ] Main Toolbar icons are not aligned (different size).
