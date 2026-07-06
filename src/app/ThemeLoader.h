#pragma once

#include "terminal_theme.h"

#include <wx/string.h>

#include <vector>

// A named terminal colour theme parsed from a shipped .toml file.
struct LoadedTheme {
  wxString name; // display name (file basename without extension)
  wxTerminalTheme theme;
};

// Scans the shipped assets directory (see ShippedAssetsDir) for *.toml theme
// files and parses each into a wxTerminalTheme. Returns them sorted by name;
// files that fail to parse are skipped (and logged). These files are read in
// place — they are NOT copied into ~/.kennel.
std::vector<LoadedTheme> LoadShippedThemes();

// Parses a single Alacritty-style TOML colour file into *out. Reads only the
// [colors.primary] / [colors.cursor] / [colors.normal] / [colors.bright]
// tables; unknown keys are ignored and font/selection fields keep their
// defaults. Returns false on a read failure or if no usable colour was found
// (in which case *out is left unchanged).
bool ParseThemeTomlFile(const wxString &path, wxTerminalTheme *out);
