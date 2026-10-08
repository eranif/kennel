#include "core/WslPath.h"

#include "core/Process.hpp"

#include <wx/filename.h>
#include <wx/utils.h>

#include <map>
#include <mutex>

namespace wsl {

wxString DistroOf(const wxString &loginShell) {
  if (!loginShell.Contains("wsl.exe")) {
    return wxEmptyString;
  }
  static const wxString kFlag = "--distribution";
  const int at = loginShell.Find(kFlag);
  if (at == wxNOT_FOUND) {
    return wxEmptyString;
  }
  wxString rest = loginShell.Mid(at + kFlag.length()).Trim(false);
  if (rest.StartsWith("\"")) {
    rest = rest.Mid(1);
    return rest.BeforeFirst('"');
  }
  return rest.BeforeFirst(' ');
}

wxString HomeDir(const wxString &distro) {
  static std::mutex mutex;
  static std::map<wxString, wxString> cache;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = cache.find(distro); it != cache.end()) {
      return it->second;
    }
  }
#ifdef __WXMSW__
  // The same switch FindShells() uses to get UTF-8 out of wsl.exe.
  ::wxSetEnv("WSL_UTF8", "1");
  auto result = Process::RunProcessAndWait(
      {R"(C:\Windows\System32\wsl.exe)", "--distribution",
       distro.ToStdString(wxConvUTF8), "-e", "sh", "-c", "echo $HOME"});
  ::wxUnsetEnv("WSL_UTF8");
  wxString home;
  if (result.ok) {
    home = wxString::FromUTF8(result.out).Trim().Trim(false);
  }
#else
  wxString home;
#endif
  if (home.empty()) {
    // Not cached: the distro may just not be ready yet. Not logged either: the
    // Logger is not thread-safe and this can run on a worker thread.
    return wxEmptyString;
  }
  std::lock_guard<std::mutex> lock(mutex);
  cache[distro] = home;
  return home;
}

namespace {
// Replaces a leading "~" with the distro's home. Empty if that is unknown.
wxString ExpandHome(const wxString &distro, const wxString &path) {
  if (path != "~" && !path.StartsWith("~/")) {
    return path;
  }
  const wxString home = HomeDir(distro);
  return home.empty() ? wxString{} : home + path.Mid(1);
}
} // namespace

wxString ResolveLinuxPath(const wxString &distro, const wxString &text,
                          const wxString &workingDir) {
  wxString path = ExpandHome(distro, text);
  if (path.empty()) {
    return wxEmptyString;
  }
  if (!path.StartsWith("/")) {
    // Relative to where the shell was started.
    wxString base = workingDir.empty() ? wxString("~") : workingDir;
    base = ExpandHome(distro, base);
    if (base.empty() || !base.StartsWith("/")) {
      return wxEmptyString;
    }
    path = base + "/" + path;
  }
  wxFileName fn(path, wxPATH_UNIX);
  fn.Normalize(wxPATH_NORM_DOTS, wxEmptyString, wxPATH_UNIX);
  return fn.GetFullPath(wxPATH_UNIX);
}

wxString ToWindowsPath(const wxString &distro, const wxString &linuxPath) {
  // /mnt/c/... is a Windows drive: go there directly, not through the share.
  if (linuxPath.StartsWith("/mnt/") && linuxPath.length() >= 6 &&
      wxIsalpha(linuxPath[5]) &&
      (linuxPath.length() == 6 || linuxPath[6] == '/')) {
    wxString path = linuxPath.Mid(6);
    path.Replace("/", "\\");
    return wxString(static_cast<wxChar>(wxToupper(linuxPath[5]))) + ":" +
           (path.empty() ? wxString("\\") : path);
  }
  wxString path = linuxPath;
  path.Replace("/", "\\");
  return "\\\\wsl.localhost\\" + distro + path;
}

} // namespace wsl
