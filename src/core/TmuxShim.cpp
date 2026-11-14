#include "core/TmuxShim.h"

#include "core/Ripgrep.h"

namespace tmuxshim {

namespace {

constexpr const char *kRelease =
    "https://github.com/eranif/tmux-shim/releases/download/v1.1.1/";

// The checksums are those of the files of release v1.1.1, checked by
// downloading them (2026-11-14). The names of the assets match `uname -m` and
// must not change: the URL is built from them. A new release is a new
// description with new checksums, never `latest`.
ToolDescription MakeTool() {
  ToolDescription tool;
  tool.name = "tmux-shim";
  tool.version = "1.1.1";
  tool.binary = "tmux-shim";
  tool.versionBanner = "tmux-shim 1.1.1";
  tool.assets = {
      {"Linux", "x86_64", wxString(kRelease) + "tmux-shim-x86_64-linux",
       "4c9287b318e547a29203501db50ae45b2990e0b91509edb27ed07a56318bc39e", ""},
      {"Linux", "aarch64", wxString(kRelease) + "tmux-shim-aarch64-linux",
       "8f9ef771b2de3e943659f0d8b3cbc82e0b2a6b85ce15907cd4176984a5c4d4e0", ""},
  };
  return tool;
}

} // namespace

const ToolDescription &Tool() {
  static const ToolDescription tool = MakeTool();
  return tool;
}

wxString InstallDir() { return ripgrep::InstallDir(); }

wxString ShellPath() { return InstallDir() + "/" + Tool().binary; }

} // namespace tmuxshim
