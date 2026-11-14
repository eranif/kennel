#pragma once

#include "core/ToolDescription.h"

#include <wx/string.h>

// tmux-shim (https://github.com/eranif/tmux-shim): the program that runs in
// the pane of tmux in front of an agent, so that the activity icon, the
// progress bar and the folder of a tab reach Kennel through tmux (which drops
// the sequences it does not know). See .agents/planning/tmux-sessions/plan.md.
//
// Kennel installs the pinned release into ~/.kennel/bin of the machine of the
// session, like ripgrep (RemoteInstaller does it: start it with Tool() and
// InstallDir()). The shim is a plain static executable, not an archive. A
// machine it has no build for (macOS, a CPU other than x86_64 and aarch64)
// gets no shim: the agent then runs in tmux without it.
namespace tmuxshim {

// The release Kennel was built for.
const ToolDescription &Tool();

// The folder it is installed into, in the notation of a shell:
// "$HOME/.kennel/bin" (the one of ripgrep).
wxString InstallDir();

// Where the program is, in the notation of a shell:
// "$HOME/.kennel/bin/tmux-shim". The same text for every machine; the shell
// that runs the command expands the `$HOME`.
wxString ShellPath();

} // namespace tmuxshim
