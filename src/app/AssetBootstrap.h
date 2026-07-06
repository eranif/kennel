#pragma once

#include "core/AppPaths.h"

// Locates the directory holding the assets shipped with the installed app
// (built-in client SVG icons, etc.). Resolution order:
//   1. Windows: <executable-path>/assets
//   2. macOS: <bundle>/Contents/Resources/assets
//   3. Linux/Unix: <prefix>/share/kennel/assets (derived from the executable),
//      with /usr/share/kennel/assets as a final fallback.
//   4. The "assets" dir next to the executable (developer/run-in-tree case).
// Returns the first existing candidate, or an empty wxFileName if none exist.
wxFileName ShippedAssetsDir();

// Resolves an icon name to an absolute file path in the shipped assets dir.
// An absolute path is returned unchanged; a bare/relative name is looked up
// in ShippedAssetsDir(). An empty iconPath returns an empty string. Existence
// is NOT checked — the caller decides on a fallback when the file is missing.
wxString ResolveIconPath(const wxString &iconPath);

// Locates the LICENSE file shipped with the installed app. Resolution order:
//   1. macOS: <bundle>/Contents/Resources/LICENSE
//   2. Windows/Linux: <executable-path>/LICENSE
// Returns an empty string if the file is not found.
wxString GetLicensePath();
