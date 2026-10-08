#pragma once

#include "core/Helpers.h"
#include <vector>
#include <wx/string.h>

// Top-level window geometry persisted in .persist.json.
struct WindowGeometry {
  int x = -1; // -1 => let the platform place the window
  int y = -1;
  int width = 1280;
  int height = 800;
  bool maximized = false;
};

// UI preferences (.persist.json). Non-essential and safely deletable: a
// missing file yields these defaults (never an error).
struct UiPrefs {
  int version = 1;
  WindowGeometry window;
  // Splitter positions, in device-independent pixels (so they survive a change
  // of display scaling). <= 0 => keep the default layout.
  int sidebarWidth{0};   // Width of the left pane (sessions) vs the terminal
  int treePaneHeight{0}; // Height of the session tree above the flat list
  wxString lastSelectedSession; // empty => no prior selection
  wxString terminalTheme;       // name of the last-applied terminal theme
  wxString terminalFontDesc;    // wxFont native info desc (empty => default)
  bool blockCursor{true};
  bool terminalOptimizedDrawing{false}; // Enable optimized drawing?
  bool checkForUpdatesOnStartup{true};
  size_t scrollbackLines{5000};
  wxString terminalLoginShell = ::FindShells().GetDefaultShellCmd();
  wxString terminalHomeDir = ::wxGetHomeDir();
  std::vector<wxString> recentWorkingDirs; // most-recent first, capped at 10
  std::vector<wxString> recentHosts;       // most-recent first, capped at 10
};
