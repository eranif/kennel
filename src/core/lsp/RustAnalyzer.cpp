#include "core/lsp/RustAnalyzer.h"

namespace rustanalyzer {

namespace {

constexpr const char *kRelease =
    "https://github.com/rust-lang/rust-analyzer/releases/download/2026-11-23/";

} // namespace

// The checksums are those of the files of the release 2026-11-23, checked by
// downloading them. On Linux and macOS the program is the .gz itself
// (compressed); on Windows it is inside a .zip.
const ToolDescription &Tool() {
  static const ToolDescription tool = [] {
    ToolDescription tool;
    tool.name = "rust-analyzer";
    tool.version = "2026-11-23";
    tool.binary = "rust-analyzer";
    tool.versionBanner = "rust-analyzer 0.3.3130-standalone";
    tool.assets = {
        {"Linux", "x86_64",
         wxString(kRelease) + "rust-analyzer-x86_64-unknown-linux-gnu.gz",
         "99748126069cc1360e2729e45ae7b563a2b885def41c0d600de8e5977a7adf13",
         ""},
        {"Linux", "aarch64",
         wxString(kRelease) + "rust-analyzer-aarch64-unknown-linux-gnu.gz",
         "cf7e9c2cd34540da92473d47f0866c9515425da01116e7aaca988c9b19408285",
         ""},
        {"Darwin", "x86_64",
         wxString(kRelease) + "rust-analyzer-x86_64-apple-darwin.gz",
         "4673e51749f937567b77813ef12621b6e11e6ba9c1b591c58f8a98fbbc9f5bac",
         ""},
        {"Darwin", "aarch64",
         wxString(kRelease) + "rust-analyzer-aarch64-apple-darwin.gz",
         "9d996784c9bc811642edd9d993c8c85e12c7111463a8e426d5e74c27da1bc0a5",
         ""},
        // This machine on Windows.
        {"Windows", "x86_64",
         wxString(kRelease) + "rust-analyzer-x86_64-pc-windows-msvc.zip",
         "6d0c3835155bb1fbee9d93d1eca4270251e08cb9a4621dbcadaf7450a6a4add2",
         "rust-analyzer.exe"},
        {"Windows", "x64",
         wxString(kRelease) + "rust-analyzer-x86_64-pc-windows-msvc.zip",
         "6d0c3835155bb1fbee9d93d1eca4270251e08cb9a4621dbcadaf7450a6a4add2",
         "rust-analyzer.exe"},
        {"Windows", "aarch64",
         wxString(kRelease) + "rust-analyzer-aarch64-pc-windows-msvc.zip",
         "c4bee363bec04ca721beb3a90a996d799ecfd94fe8128d65814f6d1ca844343d",
         "rust-analyzer.exe"},
    };
    return tool;
  }();
  return tool;
}

} // namespace rustanalyzer
