#include "app/ThemeLoader.h"

#include "app/AssetBootstrap.h"
#include "core/Logger.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/textfile.h>

#include <algorithm>

namespace {

// Strips a leading/trailing pair of matching single or double quotes.
wxString Unquote(wxString s) {
  s.Trim(true).Trim(false);
  if (s.length() >= 2) {
    const wxUniChar f = s[0];
    if ((f == '\'' || f == '"') && s.Last() == f) {
      s = s.Mid(1, s.length() - 2);
    }
  }
  return s;
}

// Assigns a parsed colour to the right wxTerminalTheme field given the current
// TOML table (e.g. "colors.normal") and key (e.g. "red"). Returns true if the
// pair mapped to a known field.
bool Assign(wxTerminalTheme *t, const wxString &table, const wxString &key,
            const wxColour &c) {
  if (table == "colors.primary") {
    if (key == "background") {
      t->bg = c;
      return true;
    }
    if (key == "foreground") {
      t->fg = c;
      return true;
    }
  } else if (table == "colors.cursor") {
    if (key == "cursor") {
      t->cursorColour = c;
      return true;
    }
    // "text" (cursor text colour) has no dedicated field; ignore.
  } else if (table == "colors.normal") {
    if (key == "black") {
      t->black = c;
      return true;
    }
    if (key == "red") {
      t->red = c;
      return true;
    }
    if (key == "green") {
      t->green = c;
      return true;
    }
    if (key == "yellow") {
      t->yellow = c;
      return true;
    }
    if (key == "blue") {
      t->blue = c;
      return true;
    }
    if (key == "magenta") {
      t->magenta = c;
      return true;
    }
    if (key == "cyan") {
      t->cyan = c;
      return true;
    }
    if (key == "white") {
      t->white = c;
      return true;
    }
  } else if (table == "colors.bright") {
    if (key == "black") {
      t->brightBlack = c;
      return true;
    }
    if (key == "red") {
      t->brightRed = c;
      return true;
    }
    if (key == "green") {
      t->brightGreen = c;
      return true;
    }
    if (key == "yellow") {
      t->brightYellow = c;
      return true;
    }
    if (key == "blue") {
      t->brightBlue = c;
      return true;
    }
    if (key == "magenta") {
      t->brightMagenta = c;
      return true;
    }
    if (key == "cyan") {
      t->brightCyan = c;
      return true;
    }
    if (key == "white") {
      t->brightWhite = c;
      return true;
    }
  }
  return false;
}

} // namespace

bool ParseThemeTomlFile(const wxString &path, wxTerminalTheme *out) {
  wxTextFile file;
  if (!file.Open(path)) {
    KLOG_WARN() << "Theme: could not open " << path;
    return false;
  }

  // Start from the existing defaults so unspecified fields stay sensible.
  wxTerminalTheme theme = *out;
  wxString table;
  int assigned = 0;

  for (wxString line = file.GetFirstLine(); !file.Eof();
       line = file.GetNextLine()) {
    line.Trim(true).Trim(false);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line[0] == '[') {
      const int close = line.Find(']');
      if (close != wxNOT_FOUND) {
        table = line.Mid(1, close - 1).Trim(true).Trim(false);
      }
      continue;
    }
    const int eq = line.Find('=');
    if (eq == wxNOT_FOUND) {
      continue;
    }
    const wxString key = line.Left(eq).Trim(true).Trim(false);
    const wxString value = Unquote(line.Mid(eq + 1));
    wxColour c(value);
    if (!c.IsOk()) {
      continue;
    }
    if (Assign(&theme, table, key, c)) {
      ++assigned;
    }
  }

  if (assigned == 0) {
    KLOG_WARN() << "Theme: no usable colours parsed from " << path;
    return false;
  }
  *out = theme;
  return true;
}

std::vector<LoadedTheme> LoadShippedThemes() {
  std::vector<LoadedTheme> themes;

  const wxFileName shipped = ShippedAssetsDir();
  if (!shipped.IsOk() || !shipped.DirExists()) {
    KLOG_INFO() << "No shipped assets directory; no themes loaded";
    return themes;
  }

  wxDir dir(shipped.GetPath());
  if (!dir.IsOpened()) {
    return themes;
  }

  wxString name;
  for (bool more = dir.GetFirst(&name, "*.toml", wxDIR_FILES); more;
       more = dir.GetNext(&name)) {
    const wxFileName fn(shipped.GetPath(), name);
    LoadedTheme lt;
    lt.name = fn.GetName(); // basename without extension
    if (ParseThemeTomlFile(fn.GetFullPath(), &lt.theme)) {
      lt.theme.isBlockCursor = true;
      lt.theme.font = wxTerminalTheme::MakeDefaultFont();
      KLOG_DEBUG() << "Loading shipped theme: " << lt.name
                   << ". Font size:" << lt.theme.font.GetPointSize();
      themes.push_back(std::move(lt));
    }
  }

  std::sort(themes.begin(), themes.end(),
            [](const LoadedTheme &a, const LoadedTheme &b) {
              return a.name.CmpNoCase(b.name) < 0;
            });
  KLOG_DEBUG() << "Loaded " << static_cast<int>(themes.size())
               << " terminal theme(s)";
  return themes;
}
