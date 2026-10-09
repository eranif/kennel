#include "app/AssetBootstrap.h"

#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/Config.h"
#include "core/Logger.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/font.h>
#include <wx/fontenum.h>
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

wxBitmapBundle SessionIconFor(const Session &session, int size) {
  if (session.plainTerminal) {
    return AppManager::Get().GetBitmaps().Get("terminal", false);
  }
  const auto *agentDef =
      AppManager::Get().Adapters().FindAgent(session.agentName);
  if (agentDef == nullptr) {
    return wxBitmapBundle{};
  }
  const wxString path = ResolveIconPath(agentDef->iconPath);
  if (path.empty() || !wxFileExists(path)) {
    return wxBitmapBundle{};
  }
  return wxBitmapBundle::FromSVGFile(path, wxSize(size, size));
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

void LoadBundledFonts() {
#if wxUSE_PRIVATE_FONTS
  // The font file and the family name it registers.
  const wxString kDefaultFontFile = "IosevkaTerm-Regular.ttf";
  const wxString kDefaultFontFace = "Iosevka Term";

#ifdef __WXMAC__
  // AddPrivateFont() accepts only Contents/Resources/Fonts here.
  wxFileName fontsDir(wxStandardPaths::Get().GetResourcesDir(), "");
  fontsDir.AppendDir("Fonts");
#else
  const wxFileName assets = ShippedAssetsDir();
  if (!assets.IsOk()) {
    return;
  }
  wxFileName fontsDir = assets;
  fontsDir.AppendDir("fonts");
#endif
  if (!fontsDir.DirExists()) {
    KLOG_WARN() << "Bundled fonts folder not found: " << fontsDir.GetPath();
    return;
  }

  wxArrayString files;
  wxDir::GetAllFiles(fontsDir.GetPath(), &files, "*.?tf", wxDIR_FILES);
  for (const wxString &file : files) {
    if (!wxFont::AddPrivateFont(file)) {
      KLOG_WARN() << "Could not load the bundled font: " << file;
      continue;
    }
    KLOG_INFO() << "Loaded the bundled font: " << file;
    if (wxFileName(file).GetFullName() == kDefaultFontFile) {
#if wxUSE_FONTENUM
      if (!wxFontEnumerator::IsValidFacename(kDefaultFontFace)) {
        KLOG_WARN() << "The system does not know the font '" << kDefaultFontFace
                    << "' after loading " << file;
        continue;
      }
#endif
      SetBundledFontFace(kDefaultFontFace);
    }
  }
#endif // wxUSE_PRIVATE_FONTS
}
