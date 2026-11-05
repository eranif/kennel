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

// What the close button of the tab of an agent session does.
enum class CloseAction {
  Ask,     // Ask: archive (so it can be restored) or close
  Archive, // Archive the session
  Close,   // Close the session
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
  // The page of the side book (file explorer, git) that was selected.
  int sideTab{0};
  wxString lastSelectedSession;      // empty => no prior selection
  wxString terminalTheme{"Mariana"}; // name of the last-applied terminal theme
  wxString terminalFontDesc;         // wxFont native info (empty => default)
  bool blockCursor{true};
  bool wrapLines{true};        // Wrap the long lines of the file editor
  bool groupTabs{true};        // Tab groups in the main book, by session group
  bool gitSignoff{false};      // The "Signoff" box of the commit dialog
  bool highlightBraces{false}; // Highlight the brace at the caret and its pair
  bool highlightIndentLines{false}; // Highlight the indent line of the block
  bool terminalOptimizedDrawing{false}; // Enable optimized drawing?
  // The theme of the application: "system", "dark", "light", or "terminal" (it
  // looks like the terminal theme). It was always dark before it could be
  // chosen.
  wxString appTheme{"terminal"};
  bool checkForUpdatesOnStartup{true};
  // The last answer to the dialog at the end of a review: also close the
  // Review Buddy?
  bool closeReviewBuddyOnDismiss{false};
  // The first run looks for claude, codex, kiro-cli and opencode on this
  // machine once; this says it did (see MainFrame::StartLocalAgentScan()). Set
  // it to false in the prefs file to scan again (a missing key counts as true,
  // see ParsePrefs()).
  bool localAgentsScanned{false};
  // The user was asked once whether to look for agents in WSL and on the SSH
  // hosts. Set it to false in the prefs file to be asked again (a missing key
  // counts as true).
  bool remoteScanOffered{false};
  // The shell the user chose last for a new terminal: the question offers it
  // first. Empty: the default shell (terminalLoginShell).
  wxString lastTerminalShell;
  bool tabHasCloseButton{false};
  // What the close button of the tab of an agent session does.
  CloseAction sessionCloseAction{CloseAction::Ask};
  size_t scrollbackLines{5000};
  wxString terminalLoginShell = ::FindShells().GetDefaultShellCmd();
  wxString terminalHomeDir = ::wxGetHomeDir();
  std::vector<wxString> recentWorkingDirs; // most-recent first, capped at 10
  std::vector<wxString> recentHosts;       // most-recent first, capped at 10
  wxString tabOutlineColour{"default"};
  wxDirection tabPosition{wxDirection::wxTOP};
  // Where the tabs of the side bar (file explorer, git) are.
  wxDirection sideBarTabPosition{wxDirection::wxTOP};
};
