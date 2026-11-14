#pragma once

#include "core/Account.h"
#include "core/ToolDescription.h"

#include <wx/string.h>

// ripgrep, the engine of Find in Files.
//
// This machine (Windows, macOS): Kennel ships its own rg next to the program.
// Linux, WSL and remote hosts (Linux or macOS, whatever this machine is):
// Kennel installs the pinned release into ~/.kennel/bin of that machine (see
// RemoteInstaller). We never use an rg that
// is somewhere else on the machine, so that the version is always the one the
// results were tested with.
namespace ripgrep {

// The release Kennel was built for.
const ToolDescription &Tool();

// The folder on a Unix machine (a remote host, a WSL distribution, this
// machine on Linux) that Kennel installs tools into, in the notation of a
// shell: "$HOME/.kennel/bin".
wxString LinuxInstallDir();

// Where the rg of `account` is, or empty if there is no place to look (the
// program is not next to Kennel). A Linux path in shell notation for a remote
// host, the Linux path for WSL, a native path for this machine.
wxString ExecutableOf(const Account &account);

// Whether Kennel puts rg on the machine of `account` itself (RemoteInstaller):
// everywhere but this machine on Windows and macOS, where rg comes with the
// program (a missing copy there is a broken installation, not something to
// install).
bool IsInstalledByKennel(const Account &account);

} // namespace ripgrep
