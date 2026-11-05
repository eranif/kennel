#pragma once

#include "core/Config.h"
#include "core/SshConfigParser.h"

#include <vector>
#include <wx/string.h>

// Looks for the AI tools Kennel supports (claude, codex, kiro-cli, opencode)
// and makes an AgentDef for each one it finds, with the arguments and the icon
// of the tool. GUI-free. ScanLocal(), ScanWsl() and ScanRemote() block (a
// remote one for seconds), so call them from a worker thread; ListWslDistros()
// and ListRemoteHosts() only read a few small files and can run on the UI
// thread.
namespace agentscan {

// A WSL distro (Windows only).
struct WslTarget {
  wxString distro;
  wxString loginShell; // The command that opens a shell in it
};

// The tools on this machine.
std::vector<AgentDef> ScanLocal();

// The WSL distros of this machine. Empty if not on Windows or if WSL is not
// installed.
std::vector<WslTarget> ListWslDistros();

// The tools in a WSL distro.
std::vector<AgentDef> ScanWsl(const WslTarget &target);

// The hosts known to this machine: the SSH config and the hosts the user added.
std::vector<SshHostEntry> ListRemoteHosts();

// The tools on a remote host (over ssh).
std::vector<AgentDef> ScanRemote(const SshHostEntry &host);

} // namespace agentscan
