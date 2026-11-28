#include "core/lsp/Phpantom.h"

namespace phpantom {

namespace {

constexpr const char *kRelease =
    "https://github.com/PHPantom-dev/phpantom_lsp/releases/download/1.1.0/";

} // namespace

// The checksums are those of the files of the release 1.1.0, checked by
// downloading them. Each archive holds the program and nothing else.
const ToolDescription &Tool() {
  static const ToolDescription tool = [] {
    ToolDescription tool;
    tool.name = "phpantom_lsp";
    tool.version = "1.1.0";
    tool.binary = "phpantom_lsp";
    tool.versionBanner = "phpantom_lsp 1.1.0";
    tool.assets = {
        {"Linux", "x86_64",
         wxString(kRelease) + "phpantom_lsp-x86_64-unknown-linux-gnu.tar.gz",
         "f374e72ebd51d5e991bb0e7e395ec3a49b3dc75f101e69e4743f908515e9d7e7",
         "phpantom_lsp"},
        {"Linux", "aarch64",
         wxString(kRelease) + "phpantom_lsp-aarch64-unknown-linux-gnu.tar.gz",
         "888633fd40b9af4709338af7cc76a34e9096f4259812cd24911109af1d068c5f",
         "phpantom_lsp"},
        {"Darwin", "x86_64",
         wxString(kRelease) + "phpantom_lsp-x86_64-apple-darwin.tar.gz",
         "6f66e490f41b2d013b3aba0abe60d5909af1a83f10417dc253a0dfbaebab7866",
         "phpantom_lsp"},
        {"Darwin", "aarch64",
         wxString(kRelease) + "phpantom_lsp-aarch64-apple-darwin.tar.gz",
         "7ce64bbb0aea4e421cbc055f1b5b0cde36eaeb0ccb14da235f136447d29c6b70",
         "phpantom_lsp"},
        // This machine on Windows.
        {"Windows", "x86_64",
         wxString(kRelease) + "phpantom_lsp-x86_64-pc-windows-msvc.zip",
         "9b5b93400171881517a36dc34ac330db733399a6faaec7e88eec18d3189d11bd",
         "phpantom_lsp.exe"},
        {"Windows", "x64",
         wxString(kRelease) + "phpantom_lsp-x86_64-pc-windows-msvc.zip",
         "9b5b93400171881517a36dc34ac330db733399a6faaec7e88eec18d3189d11bd",
         "phpantom_lsp.exe"},
        {"Windows", "aarch64",
         wxString(kRelease) + "phpantom_lsp-aarch64-pc-windows-msvc.zip",
         "d6c1b8fedca8cb01f8128f863ed5eed2abd2b8b6f00c537ab518ef74c59ace73",
         "phpantom_lsp.exe"},
    };
    return tool;
  }();
  return tool;
}

} // namespace phpantom
