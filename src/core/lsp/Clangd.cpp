#include "core/lsp/Clangd.h"

namespace clangd {

namespace {

constexpr const char *kRelease =
    "https://github.com/clangd/clangd/releases/download/23.1.0/";

// The checksums are those of the files that clangd/clangd published for 23.1.0
// (the digests of the release), checked by downloading the files.
ToolDescription MakeTool() {
  ToolDescription tool;
  tool.name = "clangd";
  tool.version = "23.1.0";
  tool.binary = "clangd";
  tool.versionBanner = "clangd version 23.1.0";
  tool.assets = {
      {"Linux", "x86_64", wxString(kRelease) + "clangd-linux-23.1.0.zip",
       "e53b1a96196095faedb7642cf64964f7fb9ad4a0c1f00dd2c172a3d9dcbafdfd",
       "clangd_23.1.0/bin/clangd"},
      // macOS: a host that is reached over ssh, or this machine.
      {"Darwin", "aarch64", wxString(kRelease) + "clangd-mac-23.1.0.zip",
       "1082e6638223b785ca2daf0939f13afcd0bb95c84ee9a4bbaff4745365159253",
       "clangd_23.1.0/bin/clangd"},
      // This machine on Windows (the only place that has these: a host that is
      // reached over ssh is not Windows).
      {"Windows", "x86_64", wxString(kRelease) + "clangd-windows-23.1.0.zip",
       "23412a240756a162e7b98a282f36aa2a23a88db5ce16a0cbc4fef7253768c810",
       "clangd_23.1.0/bin/clangd.exe"},
      {"Windows", "x64", wxString(kRelease) + "clangd-windows-23.1.0.zip",
       "23412a240756a162e7b98a282f36aa2a23a88db5ce16a0cbc4fef7253768c810",
       "clangd_23.1.0/bin/clangd.exe"},
  };
  return tool;
}

} // namespace

const ToolDescription &Tool() {
  static const ToolDescription tool = MakeTool();
  return tool;
}

} // namespace clangd
