#pragma once

#include <vector>
#include <wx/string.h>

// A program that Kennel can put on a machine for the user (ripgrep now, maybe
// clangd later): which version, and where to get it for each kind of machine.
// Everything is pinned in the source, the checksums too, so that Kennel only
// ever installs a file it was built to expect. GUI-free.
struct ToolAsset {
  // The machine this is for, as `uname -s` and `uname -m` print them.
  wxString os;   // "Linux"
  wxString arch; // "x86_64", "aarch64" (macOS's "arm64" is the second)
  wxString url;  // A .tar.gz, or a plain executable
  // SHA-256 of what `url` serves, 64 lower-case hex digits.
  wxString sha256;
  // The file to take out of the archive ("rg"). Empty: the download is the
  // executable itself.
  wxString member;
};

struct ToolDescription {
  wxString name;    // "ripgrep"
  wxString version; // "15.2.0"
  wxString binary;  // The file name the program gets on the machine: "rg"
  // What `<binary> --version` prints at the start when the right version is
  // installed. A different start means another or an older version.
  wxString versionBanner;
  std::vector<ToolAsset> assets;

  // The asset for a machine, or null if there is none (macOS, BSD, a CPU the
  // tool was not pinned for).
  const ToolAsset *FindAsset(const wxString &os, const wxString &arch) const {
    for (const ToolAsset &asset : assets) {
      if (asset.os == os && asset.arch == arch) {
        return &asset;
      }
    }
    return nullptr;
  }
};
