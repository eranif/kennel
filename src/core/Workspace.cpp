#include "core/Workspace.h"

#include "core/AdapterRegistry.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/TmuxClient.h"
#include "core/WslPath.h"

tmux::Server Session::GetTmuxServer() const {
  return plainTerminal || agentName.empty() ? tmux::Server::kTerminal
                                            : tmux::Server::kAgent;
}

Account Session::GetTmuxAccount(const std::optional<AgentDef> &agent,
                                const wxString &defaultShell) const {
  // The kind of the session comes from GetTmuxServer() only, so that the server
  // and the account can not disagree (`agent` is not looked at for a terminal).
  // The shell that runs on this machine: the one of the terminal (the one it
  // chose, else the default one), or the agent's own.
  wxString shell = defaultShell;
  if (GetTmuxServer() == tmux::Server::kTerminal) {
    if (!remoteHost.empty()) {
      // A terminal on a remote host: a local shell that logs in with ssh.
      return Account::Remote(remoteHost, remoteUser);
    }
    if (!loginShell.empty()) {
      shell = loginShell;
    }
  } else if (agent) {
    if (agent->IsRemote()) {
      return Account::Remote(agent->remoteHost, agent->remoteUser);
    }
    if (!agent->loginShell.empty()) {
      shell = agent->loginShell;
    }
  }
  if (const wxString distro = wsl::DistroOf(shell); !distro.empty()) {
    return Account::WSL(distro);
  }
  return Account::LocalHost();
}

std::optional<tmux::Target>
Session::GetTmuxTarget(const AdapterRegistry &agents,
                       const wxString &defaultShell) const {
  if (!IsTmuxSession()) {
    return std::nullopt;
  }
  std::optional<AgentDef> agent;
  if (GetTmuxServer() == tmux::Server::kAgent) {
    const AgentDef *found = agents.FindAgent(agentName);
    if (found == nullptr) {
      KLOG_WARN() << "The tmux session " << tmuxName
                  << " is left alone: the agent '" << agentName
                  << "' is not known any more";
      return std::nullopt;
    }
    agent = *found;
  }
  tmux::Target target{GetTmuxAccount(agent, defaultShell), GetTmuxServer()};
  if (!TmuxClient::IsAvailableFor(target.account)) {
    return std::nullopt;
  }
  return target;
}
