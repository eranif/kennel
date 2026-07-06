#include "app/AssetBootstrap.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

namespace {

// Returns the directory containing the running executable.
wxFileName ExecutableDir() {
  wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
  return wxFileName::DirName(exe.GetPath());
}

} // namespace

wxFileName ShippedAssetsDir() {
  std::vector<wxFileName> candidates;

#if defined(__WXMSW__)
  // Windows: assets ship in an "assets" dir next to the executable.
  {
    wxFileName win = ExecutableDir();
    win.RemoveLastDir(); // Pop bin
    win.AppendDir("assets");
    candidates.push_back(win);
  }
#elif defined(__WXMAC__)
  // <bundle>/Contents/Resources/assets
  wxFileName mac(wxStandardPaths::Get().GetResourcesDir(), "");
  mac.AppendDir("assets");
  candidates.push_back(mac);
#endif

  // <prefix>/share/kennel/assets, where <prefix> is derived from the
  // executable location (e.g. /usr/bin/kennel -> /usr/share/kennel/assets).
  {
    wxFileName prefix = ExecutableDir(); // .../bin
    prefix.RemoveLastDir();              // .../  (the install prefix)
    wxFileName share = prefix;
    share.AppendDir("share");
    share.AppendDir("kennel");
    share.AppendDir("assets");
    candidates.push_back(share);
  }

  // Developer / run-in-tree: an "assets" dir beside the executable.
  {
    wxFileName local = ExecutableDir();
    local.AppendDir("assets");
    candidates.push_back(local);
  }

  // Final Linux fallback.
  candidates.push_back(wxFileName::DirName("/usr/share/kennel/assets"));

  for (const wxFileName &c : candidates) {
    if (c.DirExists()) {
      return c;
    }
  }
  return wxFileName();
}

wxString ResolveIconPath(const wxString &iconPath) {
  if (iconPath.empty()) {
    return wxString();
  }
  wxFileName fn(iconPath);
  if (fn.IsAbsolute()) {
    return iconPath;
  }
  const wxFileName shipped = ShippedAssetsDir();
  if (!shipped.IsOk()) {
    return wxString();
  }
  return wxFileName(shipped.GetFullPath(), iconPath).GetFullPath();
}

wxString GetLicensePath() {
  std::vector<wxFileName> candidates;

#if defined(__WXMAC__)
  // macOS: <bundle>/Contents/Resources/LICENSE
  wxFileName mac(wxStandardPaths::Get().GetResourcesDir(), "LICENSE");
  candidates.push_back(mac);
#endif

  // Windows/Linux: LICENSE next to the executable
  {
    wxFileName exe = ExecutableDir();
    exe.SetName("LICENSE");
    candidates.push_back(exe);
  }

  // Developer / run-in-tree: LICENSE in the source root
  {
    wxFileName src = ExecutableDir();
    src.RemoveLastDir();
    src.RemoveLastDir();
    src.SetName("LICENSE");
    candidates.push_back(src);
  }

  for (const wxFileName &c : candidates) {
    if (c.FileExists()) {
      return c.GetFullPath();
    }
  }
  return wxString();
}
