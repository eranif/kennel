#include "core/Helpers.h"
#include "core/Logger.h"

#ifdef __WXMSW__

#include <wx/arrstr.h>
#include <wx/filename.h>
#include <wx/msw/registry.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

namespace {
constexpr const char *kMSYS2RegistryNameV1 = "MSYS2 64bit";
constexpr const char *kMSYS2RegistryNameV2 = "MSYS2";
} // namespace

static bool m_checked_for_install_dir{false};
static bool m_checked_for_home_dir{false};
static std::optional<wxString> m_install_dir;
static std::optional<wxString> m_home_dir;

std::optional<wxString> FindInstallDir() {
  if (m_checked_for_install_dir) {
    return m_install_dir;
  }

  m_checked_for_install_dir = true;

  wxString reg_install_path;
  wxRegKey uninstall(wxRegKey::HKCU,
                     R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall)");
  wxString appname;
  long dummy;
  bool cont = uninstall.GetFirstKey(appname, dummy);
  while (cont) {
    wxString display_name;
    wxRegKey appkey(wxRegKey::HKCU,
                    R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\)" +
                        appname);
    if (appkey.QueryValue("DisplayName", display_name) &&
        (display_name == kMSYS2RegistryNameV1 ||
         display_name == kMSYS2RegistryNameV2)) {
      appkey.QueryValue("InstallLocation", reg_install_path);
      break;
    }
    cont = uninstall.GetNextKey(appname, dummy);
  }

  if (!reg_install_path.empty()) {
    m_install_dir = reg_install_path;
    return m_install_dir;
  } else {
    // try common paths
    std::vector<wxString> vpaths = {R"(C:\msys64)", R"(C:\msys2)",
                                    R"(C:\msys)"};
    for (const wxString &path : vpaths) {
      if (wxFileName::DirExists(path)) {
        m_install_dir = path;
        return m_install_dir;
      }
    }
  }
  return std::nullopt;
}

std::optional<wxString> FindHomeDir() {
  const auto msyspath = FindInstallDir();
  if (!msyspath) {
    return std::nullopt;
  }

  if (m_checked_for_home_dir) {
    return m_home_dir;
  }

  m_checked_for_home_dir = true;

  wxFileName cargo_dir{*msyspath, wxEmptyString};
  cargo_dir.AppendDir("home");
  cargo_dir.AppendDir(::wxGetUserId());

  if (cargo_dir.DirExists()) {
    m_home_dir = cargo_dir.GetPath();
  }
  return m_home_dir;
}

wxString GetPath(bool useSystemPath) {
  const auto msyspath = FindInstallDir();

  wxArrayString paths_to_try;

  // next in order are is the PATH environment variable
  if (useSystemPath) {
    wxString pathenv;
    wxGetEnv("PATH", &pathenv);
    paths_to_try = ::wxStringTokenize(pathenv, ";", wxTOKEN_STRTOK);
  }

  // if we have msys2 installed, add the bin folder (we place them at start)
  if (msyspath) {
    const wxString m_chroots[] = {
        "\\clang64",
        "\\clangarm64",
        "\\usr",
        "\\",
    };
    for (const auto &root : m_chroots) {
      paths_to_try.Insert(*msyspath + root + R"(\bin)", 0);
    }
  }

  // local (Windows native path)
  // e.g. C:\Users\user\.local\bin
  wxFileName local_native_bin{"C:\\Users", wxEmptyString};
  local_native_bin.AppendDir(::wxGetUserId());
  local_native_bin.AppendDir(".local");
  local_native_bin.AppendDir("bin");

  if (local_native_bin.DirExists()) {
    paths_to_try.Add(local_native_bin.GetPath());
  }

  // Finally, add the executable path.
  paths_to_try.push_back(
      wxFileName{wxStandardPaths::Get().GetExecutablePath()}.GetPath());
  return ::wxJoin(paths_to_try, ';');
}

namespace platform {
std::optional<wxString> Which(const wxString &command, bool useSystemPath) {
  wxString path = GetPath(useSystemPath);

  wxArrayString paths_to_try = ::wxStringTokenize(path, ";", wxTOKEN_STRTOK);
  static const wxString exts[] = {".exe", ".cmd"};
  // at the point, the order of search is:
  // MSYS2 -> Executable path -> PATH paths
  for (const auto &path : paths_to_try) {
    if (!wxFileName::DirExists(path)) {
      continue;
    }
    for (const wxString &ext : exts) {
      wxString exepath = path;
      exepath << "\\" << command << ext;
      if (wxFileName::FileExists(exepath)) {
        return exepath;
      }
    }
  }
  return std::nullopt;
}
} // namespace platform
#else

namespace platform {
#include <wx/filename.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

std::optional<wxString> Which(const wxString &command, bool useSystemPath) {
  if (command.empty()) {
    return std::nullopt;
  }

  // If it's already an absolute path and is executable, return it directly.
  if (command.StartsWith("/") && wxFileName::FileExists(command)) {
    return command;
  }

  KLOG_DEBUG() << "Using system path: " << useSystemPath;
  wxString pathEnv;
  if (useSystemPath) {
    wxGetEnv("PATH", &pathEnv);
    KLOG_DEBUG() << "System PATH is: " << pathEnv;
  }

  // Append common directories that may not be in PATH (e.g. when launched
  // from a GUI on macOS where the shell profile hasn't run).
  const wxString home = ::wxGetHomeDir();
  const wxString extraDirs[] = {
      home + "/.local/bin", home + "/.cargo/bin", home + "/.nvm/current/bin",
      home + "/.volta/bin", home + "/.bun/bin",   home + "/go/bin",
      "/usr/local/bin",     "/opt/homebrew/bin",  "/opt/homebrew/sbin",
      "/usr/local/go/bin",  "/snap/bin",          home + "/.toolbox/bin",
  };

  for (const wxString &dir : extraDirs) {
    if (!pathEnv.Contains(dir) && wxDirExists(dir)) {
      pathEnv << ":" << dir;
    }
  }

  KLOG_DEBUG() << "Searching for: " << command << " in path: " << pathEnv;
  wxArrayString dirs = wxStringTokenize(pathEnv, ":", wxTOKEN_STRTOK);
  for (const wxString &dir : dirs) {
    wxFileName candidate(dir, command);
    if (candidate.FileExists() && candidate.IsFileExecutable()) {
      return candidate.GetFullPath();
    }
  }
  return std::nullopt;
}
} // namespace platform
#endif

namespace {
std::vector<std::pair<wxString, wxString>> FindWSL() {
  std::vector<std::pair<wxString, wxString>> terminals;
#ifdef __WXMSW__
  const std::string WSLExecutable = R"(C:\Windows\System32\wsl.exe)";
  // Custom code to handle WSL terminals
  if (!wxFileExists(wxString::FromUTF8(WSLExecutable)))
    return {};

  KLOG_DEBUG() << "Found WSL:" << WSLExecutable;

  // List the available distros we got.
  std::vector<std::string> command = {
      WSLExecutable,
      "-l",
      "-q",
  };
  wxString list_output;

  ::wxSetEnv("WSL_UTF8", "1");
  auto result = Process::RunProcessAndWait(command);
  ::wxUnsetEnv("WSL_UTF8");

  if (!result.ok)
    return {};

  list_output = wxString::FromUTF8(result.out);
  KLOG_DEBUG() << list_output;

  wxArrayString lines = wxStringTokenize(list_output, "\r\n", wxTOKEN_STRTOK);
  for (auto &output_line : lines) {
    output_line.Trim().Trim(false);
    wxString lc_line = output_line.Lower();
    if (lc_line.Contains("windows subsystem for linux distributions") ||
        lc_line.empty()) {
      KLOG_DEBUG() << "Ignoring line: [" << lc_line << "]";
      continue;
    }

    KLOG_DEBUG() << "  Checking WSL distro: '" << output_line << "'"
                 << output_line.size() << " bytes";

    wxString title;
    title << "WSL: " << output_line;
    wxString cmd;
    cmd << WSLExecutable << " --distribution "
        << WrapWithDoubleQuotes(output_line) << " --cd %WORKING_DIRECTORY%";
    KLOG_DEBUG() << "Adding:" << title << "=>" << cmd;
    terminals.push_back(std::make_pair(title, cmd));
  }
#endif // __WXMSW__
  return terminals;
}
} // namespace

FindShellResult FindShells() {
  static std::optional<FindShellResult> result{std::nullopt};
  if (!result.has_value()) {
    std::vector<std::pair<wxString, wxString>> shells;
    int defaultShell{0};
#if defined(__WXMAC__) || defined(__WXGTK__)
    shells.push_back({"/bin/bash", "/bin/bash --login -i"});
#else
    shells.push_back({"CMD.EXE", LR"(C:\Windows\System32\cmd.exe)"});
    auto wsl_shells = FindWSL();
    shells.insert(shells.end(), wsl_shells.begin(), wsl_shells.end());

    bool hashMsys2{false};
    auto bash = platform::Which("bash", false);
    if (bash) {
      shells.push_back(
          {"MSYS2: Bash", wxString::Format("%s --login -i", *bash)});
      hashMsys2 = true;
    }

    for (size_t i = 0; i < shells.size(); ++i) {
      const wxString &name = shells[i].second;
      if (!hashMsys2) {
        if (name.Lower().Contains("ubuntu")) {
          defaultShell = i;
          break;
        }

      } else if (shells[i].first == "MSYS2: Bash") {
        defaultShell = i;
        break;
      }
    }
#endif
    result = FindShellResult{
        .shells = std::move(shells),
        .defaultShell = defaultShell,
    };
  }
  return *result;
}

wxArrayString FindShellNames() {
  const auto &allShells = FindShells().shells;
  wxArrayString result;
  result.reserve(allShells.size());
  for (const auto &[name, _] : allShells) {
    result.Add(name);
  }
  return result;
}

std::optional<wxString> FindShellNameByCommand(const wxString &shellCommand) {
  const auto &allShells = FindShells().shells;
  for (const auto &[name, cmd] : allShells) {
    if (cmd == shellCommand) {
      return name;
    }
  }
  return std::nullopt;
}

std::optional<wxString> FindShellCommand(const wxString &shellName) {
  const auto &allShells = FindShells().shells;
  for (const auto &[name, cmd] : allShells) {
    if (name == shellName) {
      return cmd;
    }
  }
  return std::nullopt;
}
