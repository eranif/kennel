#include "ThemeManager.h"

#include "core/Logger.h"

ThemeManager &ThemeManager::Get() {
  static ThemeManager instance;
  return instance;
}

void ThemeManager::Initialize(std::vector<LoadedTheme> themes,
                              const wxString &themeName) {
  m_themes = std::move(themes);
  m_currentTheme = themeName;
}

std::optional<wxTerminalTheme> ThemeManager::ActiveTheme() const {
  for (const auto &t : m_themes) {
    if (t.name == m_currentTheme) {
      return t.theme;
    }
  }
  return std::nullopt;
}

std::optional<wxTerminalTheme> ThemeManager::SetTheme(const wxString &name) {
  for (const auto &t : m_themes) {
    if (t.name == name) {
      m_currentTheme = name;
      return t.theme;
    }
  }
  KLOG_WARN() << "ThemeManager::SetTheme: unknown theme '" << name << "'";
  return std::nullopt;
}

std::optional<wxTerminalTheme> ThemeManager::SetFont(const wxFont &font) {
  if (!font.IsOk()) {
    return std::nullopt;
  }
  for (auto &t : m_themes) {
    t.theme.font = font;
  }
  return ActiveTheme();
}

void ThemeManager::SetBlockCursor(bool blockCursor) {
  for (auto &theme : m_themes) {
    theme.theme.isBlockCursor = blockCursor;
  }
}
