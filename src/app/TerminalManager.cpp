#include "app/TerminalManager.hpp"

#include "ThemeManager.h"
#include "core/AppManager.h"
#include "core/Logger.h"

#include <wx/utils.h>
#include <wx/window.h>

TerminalManager &TerminalManager::Get() {
  static TerminalManager manager;
  return manager;
}

wxTerminalViewCtrl *
TerminalManager::CreateTerminal(wxWindow *parent,
                                const TerminalOptions &options) {
  wxString shellCommand = options.shellCommand;
  shellCommand.Replace("%WORKING_DIRECTORY%", options.workingDir.empty()
                                                  ? wxString("~")
                                                  : options.workingDir);

  KLOG_DEBUG() << "Running shell: " << shellCommand;
  auto *terminal = new wxTerminalViewCtrl(
      parent, shellCommand, BuildEnvironment(options.env), options.cwd);

  const auto &prefs = AppManager::Get().GetPrefs();
  terminal->EnableSafeDrawing(!prefs.terminalOptimizedDrawing);
  if (auto theme = ThemeManager::Get().ActiveTheme()) {
    terminal->SetTheme(*theme);
  }
  terminal->SetBufferSize(prefs.scrollbackLines);
  // The terminal takes the focus when the mouse goes down in it (it does so
  // on release too), so the one the user clicks is the one that gets the keys.
  terminal->Bind(wxEVT_LEFT_DOWN, [terminal](wxMouseEvent &event) {
    event.Skip();
    if (!terminal->HasFocus()) {
      terminal->SetFocus();
    }
  });
  return terminal;
}

std::optional<wxTerminalViewCtrl::EnvironmentList>
TerminalManager::BuildEnvironment(
    const std::map<wxString, wxString> &overrides) const {
  if (overrides.empty()) {
    return std::nullopt;
  }

  wxEnvVariableHashMap env;
  wxGetEnvMap(&env);
  for (const auto &[key, value] : overrides) {
    env[key] = value;
  }

  wxTerminalViewCtrl::EnvironmentList list;
  list.reserve(env.size());
  for (const auto &entry : env) {
    wxString line;
    line << entry.first << "=" << entry.second;
    list.push_back(line.ToStdString(wxConvUTF8));
  }
  return list;
}
