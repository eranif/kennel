#include "core/Ripgrep.h"

#include "core/WslPath.h"

#include <wx/filename.h>

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
      // macOS: a host that is reached over ssh, or this machine.
      {"Darwin", "x86_64",
       wxString(kRelease) + "ripgrep-15.2.0-x86_64-apple-darwin.tar.gz",
       "af7825fcc69a2afc7a7aea55fc9af90e26421d8f20fe59df32e233c0b8a231c1",
       "rg"},
      {"Darwin", "aarch64",
       wxString(kRelease) + "ripgrep-15.2.0-aarch64-apple-darwin.tar.gz",
       "3750b2e93f37e0c692657da574d7019a101c0084da05a790c83fd335bad973e4",
       "rg"},
      // This machine on Windows (the only place that has these: a host that is
      // reached over ssh is not Windows, see Ripgrep.h).
      {"Windows", "x86_64",
       wxString(kRelease) + "ripgrep-15.2.0-x86_64-pc-windows-msvc.zip",
       "71b2fef860abe467217a538ff31de02f5258807c0129f771846f87bd029aafc5",
       "rg.exe"},
      {"Windows", "x64",
       wxString(kRelease) + "ripgrep-15.2.0-x86_64-pc-windows-msvc.zip",
       "71b2fef860abe467217a538ff31de02f5258807c0129f771846f87bd029aafc5",
       "rg.exe"},
      {"Windows", "aarch64",
       wxString(kRelease) + "ripgrep-15.2.0-aarch64-pc-windows-msvc.zip",
       "e4abca10c3a64ebea742667dd7009449d49403db5460dd6873e389fa2945360f",
       "rg.exe"},
  };
  return tool;
}

} // namespace

const ToolDescription &Tool() {
  static const ToolDescription tool = MakeTool();
  return tool;
}

wxString InstallDir() { return "$HOME/.kennel/bin"; }

wxString ExecutableOf(const Account &account) {
  const ToolDescription &tool = Tool();
  switch (account.m_type) {
  case HostType::kRemote:
    return InstallDir() + "/" + tool.binary;
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
  // Installed by RemoteInstaller, like on any other machine: the same version
  // everywhere, whatever the system. A copy that happens to be next to Kennel
  // or on the PATH is not used.
  wxFileName installed(wxFileName::GetHomeDir(), binary);
  installed.AppendDir(".kennel");
  installed.AppendDir("bin");
  return installed.FileExists() ? installed.GetFullPath() : wxString();
}

} // namespace ripgrep
