#pragma once

#include "core/Helpers.h"
#include <vector>
#include <wx/arrstr.h>
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
  // The layout of the panes: the wxAuiManager perspective string. Empty => the
  // default layout. The sizes in it are in device pixels, so after a change of
  // display scaling the side panes keep their pixel width.
  wxString auiLayout;
  wxString lastSelectedSession;      // empty => no prior selection
  wxString terminalTheme{"Cobalt2"}; // name of the last-applied terminal theme
  wxString terminalFontDesc;         // wxFont native info (empty => default)
  bool blockCursor{true};
  bool terminalOptimizedDrawing{false}; // Enable optimized drawing?
  // The theme of the application: "system", "dark", "light", or "terminal" (it
  // looks like the terminal theme). It was always dark before it could be
  // chosen.
  wxString appTheme{"terminal"};
  bool checkForUpdatesOnStartup{true};
  // The last answer to the dialog at the end of a review: also close the
  // Review Buddy?
  bool closeReviewBuddyOnDismiss{false};
  bool tabHasCloseButton{false};
  size_t scrollbackLines{5000};
  wxString terminalLoginShell = ::FindShells().GetDefaultShellCmd();
  wxString terminalHomeDir = ::wxGetHomeDir();
  std::vector<wxString> recentWorkingDirs; // most-recent first, capped at 10
  std::vector<wxString> recentHosts;       // most-recent first, capped at 10
  wxString tabOutlineColour{"Selection"};
  wxDirection tabPosition{wxDirection::wxBOTTOM};
};
