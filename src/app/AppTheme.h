#pragma once

#include "core/Logger.h"

#include <wx/app.h>
#include <wx/string.h>

// The theme of the application (light or dark) as the user picks it in the
// settings: "system", "dark" or "light" (UiPrefs::appTheme).

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

// Applies the theme to the running application.
inline void ApplyAppTheme(const wxString &name) {
  if (wxTheApp == nullptr) {
    return;
  }
  if (wxTheApp->SetAppearance(ParseAppTheme(name)) ==
      wxApp::AppearanceResult::Failure) {
    KLOG_WARN() << "Could not set the application theme to '" << name << "'";
  }
}
