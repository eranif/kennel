#pragma once

#include "ThemeLoader.h"
#include "terminal_theme.h"

#include <optional>
#include <vector>
#include <wx/arrstr.h>

// Process-wide owner of the loaded terminal themes and the currently-active
// selection. Accessible via ThemeManager::Get() so any app component
// (MainView, SessionPage, etc.) can read the active theme without threading
// a dependency through every constructor.
//
// Lifetime: call Initialize() once at startup (MainView ctor). Get() aborts
// before that.
class ThemeManager {
public:
  static ThemeManager &Get();

  // Loads shipped themes, applies the saved font, and sets the initial theme
  // name. Must be called once before any other method.
  void Initialize(std::vector<LoadedTheme> themes, const wxString &themeName);

  const std::vector<LoadedTheme> &Themes() const { return m_themes; }

  wxArrayString Names() const {
    wxArrayString themeNames;
    themeNames.reserve(m_themes.size());
    for (const auto &theme : m_themes) {
      themeNames.Add(theme.name);
    }
    return themeNames;
  }

  void SetBlockCursor(bool blockCursor);

  // Returns the wxTerminalTheme for the current selection, or nullopt if
  // nothing is loaded yet.
  std::optional<wxTerminalTheme> ActiveTheme() const;

  // Switches the active theme by name. No-op if the name is unknown.
  // Returns the resolved theme, or nullopt if not found.
  std::optional<wxTerminalTheme> SetTheme(const wxString &name);

  // Applies the font to every loaded theme entry and re-resolves the active
  // theme. Call after the user picks a new font.
  std::optional<wxTerminalTheme> SetFont(const wxFont &font);

  const wxString &CurrentThemeName() const { return m_currentTheme; }

private:
  ThemeManager() = default;

  std::vector<LoadedTheme> m_themes;
  wxString m_currentTheme;
};
