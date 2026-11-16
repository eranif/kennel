#pragma once

#include "terminal_view.h"

#include <map>
#include <optional>
#include <wx/string.h>

class wxWindow;

// What a terminal is made from.
struct TerminalOptions {
  // The command that starts the shell (or the ssh client, or wsl.exe...).
  // "%WORKING_DIRECTORY%" in it is replaced with `workingDir`.
  wxString shellCommand;
  // The folder that the shell runs in, as the shell sees it (a Linux path for
  // WSL or a remote host). Empty: "~".
  wxString workingDir;
  // The folder that the process of this machine starts in. Unset: where Kennel
  // is. Not for a remote host: the local shell that logs in there starts
  // where it starts.
  std::optional<wxString> cwd;
  // Added to the environment of this process, for the process of the terminal.
  // Empty: the terminal gets the environment as it is.
  std::map<wxString, wxString> env;
};

// Makes the terminals of the application. A terminal made here has the
// settings of the preferences and the colours of the active theme; what the
// owner does with it (the layout, the events it listens to, when it starts) is
// the owner's.
class TerminalManager {
public:
  static TerminalManager &Get();

  // A terminal in `parent`, made from `options`. The parent owns it. It is not
  // started: the owner calls EnsureStarted() on it when it is ready (the
  // layout, the events and the theme are in place).
  wxTerminalViewCtrl *CreateTerminal(wxWindow *parent,
                                     const TerminalOptions &options);

private:
  // The environment of this process, with `overrides` on top of it; nothing
  // when there are none (the terminal then takes the environment as it is).
  std::optional<wxTerminalViewCtrl::EnvironmentList>
  BuildEnvironment(const std::map<wxString, wxString> &overrides) const;
};
