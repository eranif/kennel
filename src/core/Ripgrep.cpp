#include "core/Ripgrep.h"

#include "core/WslPath.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

namespace ripgrep {

namespace {

constexpr const char *kRelease =
    "https://github.com/BurntSushi/ripgrep/releases/download/15.2.0/";

// The checksums are those of the files that BurntSushi/ripgrep published for
// 15.2.0 (the .sha256 files of the release), checked by downloading the files.
ToolDescription MakeTool() {
  ToolDescription tool;
  tool.name = "ripgrep";
  tool.version = "15.2.0";
  tool.binary = "rg";
  tool.versionBanner = "ripgrep 15.2.0";
  tool.assets = {
      {"Linux", "x86_64",
       wxString(kRelease) + "ripgrep-15.2.0-x86_64-unknown-linux-musl.tar.gz",
       "33e15bcf1624b25cdd2a55813a47a2f95dbe126268203e76aa6a585d1e7b149c",
       "rg"},
      {"Linux", "aarch64",
       wxString(kRelease) + "ripgrep-15.2.0-aarch64-unknown-linux-musl.tar.gz",
       "800b1e7206afe799dfb5a6901f23147cfaabe0e52210538100f61e86e1740915",
       "rg"},
  };
  return tool;
}

} // namespace

const ToolDescription &Tool() {
  static const ToolDescription tool = MakeTool();
  return tool;
}

wxString LinuxInstallDir() { return "$HOME/.kennel/bin"; }

wxString ExecutableOf(const Account &account) {
  const ToolDescription &tool = Tool();
  switch (account.m_type) {
  case HostType::kRemote:
    return LinuxInstallDir() + "/" + tool.binary;
  case HostType::kWSL: {
    const wxString home = wsl::HomeDir(account.m_name);
    return home.empty() ? wxString() : home + "/.kennel/bin/" + tool.binary;
  }
  case HostType::kLocalHost:
    break;
  }

#ifdef __WXMSW__
  const wxString binary = tool.binary + ".exe";
#else
  const wxString binary = tool.binary;
#endif
  // Next to the program (the installer and the app bundle put it there).
  wxFileName beside(wxStandardPaths::Get().GetExecutablePath());
  beside.SetFullName(binary);
  if (beside.FileExists()) {
    return beside.GetFullPath();
  }
  // Linux has no bundled copy: it is installed like on a remote host.
#if !defined(__WXMSW__) && !defined(__WXOSX__)
  wxFileName installed(wxFileName::GetHomeDir(), binary);
  installed.AppendDir(".kennel");
  installed.AppendDir("bin");
  if (installed.FileExists()) {
    return installed.GetFullPath();
  }
#endif
  return wxString();
}

} // namespace ripgrep
