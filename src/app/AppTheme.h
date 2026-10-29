#pragma once

#include "app/ThemeLoader.h"
#include "app/ThemeManager.h"
#include "core/AppManager.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <optional>
#include <wx/app.h>
#include <wx/string.h>

// The theme of the application (light or dark) as the user picks it in the
// settings: "system", "dark", "light", or "terminal" to look like the terminal
// theme (UiPrefs::appTheme).

inline constexpr const char *kAppThemeFollowTerminal = "terminal";

inline wxApp::Appearance ParseAppTheme(const wxString &name) {
  if (name == "dark") {
    return wxApp::Appearance::Dark;
  }
  if (name == "light") {
    return wxApp::Appearance::Light;
  }
  return wxApp::Appearance::System;
}

inline wxString AppThemeName(wxApp::Appearance appearance) {
  switch (appearance) {
  case wxApp::Appearance::Dark:
    return "dark";
  case wxApp::Appearance::Light:
    return "light";
  case wxApp::Appearance::System:
    break;
  }
  return "system";
}

// The background of the terminal theme in use. At startup the themes are not
// loaded yet (MainView does that), so read the saved one from the shipped
// files.
inline std::optional<wxColour> TerminalThemeBackground() {
  if (auto active = ThemeManager::Get().ActiveTheme()) {
    return active->bg;
  }
  const wxString name = AppManager::Get().GetPrefs().terminalTheme;
  for (const auto &theme : LoadShippedThemes()) {
    if (theme.name == name) {
      return theme.theme.bg;
    }
  }
  return std::nullopt;
}

// Dark or light as the terminal theme looks; the system setting if it is not
// known.
inline wxApp::Appearance ResolveAppTheme(const wxString &name) {
  if (name != kAppThemeFollowTerminal) {
    return ParseAppTheme(name);
  }
  if (auto bg = TerminalThemeBackground()) {
    return IsDarkColour(*bg) ? wxApp::Appearance::Dark
                             : wxApp::Appearance::Light;
  }
  return wxApp::Appearance::System;
}

// Applies the theme to the running application.
inline void ApplyAppTheme(const wxString &name) {
  if (wxTheApp == nullptr) {
    return;
  }
  const wxApp::Appearance appearance = ResolveAppTheme(name);
  KLOG_DEBUG() << "Application theme '" << name << "' -> '"
               << AppThemeName(appearance) << "'";
  if (wxTheApp->SetAppearance(appearance) == wxApp::AppearanceResult::Failure) {
    KLOG_WARN() << "Could not set the application theme to '" << name << "'";
  }
}
