#pragma once

#include "core/Account.h"
#include "core/ToolDescription.h"

#include <wx/string.h>

// ripgrep, the engine of Find in Files.
//
// Kennel installs the pinned release into ~/.kennel/bin of the machine of the
// search: this machine (Windows, macOS or Linux), a WSL distribution, or a
// remote host that runs Linux or macOS (see RemoteInstaller). We never use an
// rg that is somewhere else on the machine, so that the version is always the
// one the results were tested with, on every system.
namespace ripgrep {

// The release Kennel was built for.
const ToolDescription &Tool();

// The folder that Kennel installs tools into, on any machine, in the notation
// of a shell: "$HOME/.kennel/bin" (RemoteInstaller takes it for this machine on
// Windows too).
wxString InstallDir();

// Where the rg of `account` is, or empty if it is not installed there (or the
// home folder is not known). A Linux path in shell notation for a remote
// host, the Linux path for WSL, a native path for this machine.
wxString ExecutableOf(const Account &account);

} // namespace ripgrep
