#include "core/AgentScanner.h"

#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/Helpers.h"
#include "core/KennelRemote.h"
#include "core/WslPath.h"

#include <optional>
#include <set>
#include <vector>
#include <wx/filename.h>
#include <wx/tokenzr.h>

namespace agentscan {
namespace {

struct Tool {
  const char *executable; // What is run
  const char *title;      // The start of the name of the agent
};

constexpr Tool kTools[] = {
    {"claude", "Claude"},
    {"codex", "Codex"},
    {"kiro-cli", "Kiro"},
};

const char *TitleOf(const wxString &executable) {
  const wxString name = wxFileName(executable).GetName().Lower();
  for (const Tool &tool : kTools) {
    if (name == tool.executable) {
      return tool.title;
    }
  }
  return nullptr;
}

// An agent for the tool `executable`; null if it is not one we support. `where`
// is added to the name ("WSL: Ubuntu", a host...).
std::optional<AgentDef> MakeAgent(const wxString &executable,
                                  const wxString &where) {
  const char *title = TitleOf(executable);
  if (title == nullptr) {
    return std::nullopt;
  }
  AgentDef agent;
  agent.name = where.empty() ? wxString(title)
                             : wxString::Format("%s (%s)", title, where);
  agent.executable = executable;
  ApplyClientDefaults(agent);
  return agent;
}

} // namespace

std::vector<AgentDef> ScanLocal() {
  std::vector<AgentDef> found;
  for (const Tool &tool : kTools) {
    const auto path = platform::Which(tool.executable);
    if (!path) {
      continue;
    }
    if (auto agent = MakeAgent(*path, wxEmptyString)) {
      agent->iconPath = DefaultIconFor(*agent);
      found.push_back(std::move(*agent));
    }
  }
  return found;
}

std::vector<WslTarget> ListWslDistros() {
  std::vector<WslTarget> targets;
#ifdef __WXMSW__
  for (const auto &[name, command] : FindShells().shells) {
    if (!command.Contains("wsl.exe")) {
      continue;
    }
    const wxString distro = wsl::DistroOf(command);
    if (!distro.empty()) {
      targets.push_back({distro, command});
    }
  }
#endif
  return targets;
}

std::vector<AgentDef> ScanWsl(const WslTarget &target) {
  std::vector<AgentDef> found;
#ifdef __WXMSW__
  // An interactive login shell, so that the PATH set in .profile and in
  // .bashrc (nvm, ~/.local/bin...) is the one the user's terminal has.
  wxString tools;
  for (const Tool &tool : kTools) {
    tools << " " << tool.executable;
  }
  // Process joins the arguments as they are, without quoting: the distro name
  // and the script must carry their own quotes.
  auto output = RunProcessWithTimeout(
      {R"(C:\Windows\System32\wsl.exe)", "--distribution",
       WrapWithDoubleQuotes(target.distro).ToStdString(wxConvUTF8), "-e",
       "bash", "-lic",
       ("\"command -v" + tools + "; true\"").ToStdString(wxConvUTF8)},
      30);
  if (!output.ok()) {
    return found;
  }
  std::set<wxString> seen;
  const auto lines = wxStringTokenize(wxString::FromUTF8(output.value()),
                                      "\r\n", wxTOKEN_STRTOK);
  for (wxString line : lines) {
    line.Trim().Trim(false);
    // Only the paths: a shell may print a warning or a banner as well.
    if (!line.StartsWith("/")) {
      continue;
    }
    const wxString name = wxFileName(line).GetFullName();
    if (!seen.insert(name).second) {
      continue;
    }
    // The name alone: the shell finds it like it does for the user.
    if (auto agent = MakeAgent(name, "WSL: " + target.distro)) {
      agent->loginShell = target.loginShell;
      agent->iconPath = DefaultIconFor(*agent);
      found.push_back(std::move(*agent));
    }
  }
#else
  wxUnusedVar(target);
#endif
  return found;
}

std::vector<SshHostEntry> ListRemoteHosts() {
  std::vector<SshHostEntry> hosts;
  std::set<wxString> seen;
  const auto add = [&](const std::vector<SshHostEntry> &entries) {
    for (const SshHostEntry &entry : entries) {
      if (!entry.address.empty() && seen.insert(entry.address).second) {
        hosts.push_back(entry);
      }
    }
  };
  if (auto result = SshConfigParser::Default().Parse(); result.ok()) {
    add(result.value());
  }
  if (auto result = AppManager::Get().Hosts().Load(); result.ok()) {
    add(result.value());
  }
  return hosts;
}

std::vector<AgentDef> ScanRemote(const SshHostEntry &host) {
  std::vector<AgentDef> found;
  KennelRemote remote(host.address, wxEmptyString);
  remote.SetBatchMode(true); // Nobody is there to type a password
  std::set<wxString> seen;
  for (const auto &cli : remote.FindCli()) {
    if (TitleOf(cli.path) == nullptr || !seen.insert(cli.name).second) {
      continue; // The script knows more tools than Kennel supports
    }
    const wxString where = host.name.empty() ? host.address : host.name;
    if (auto agent = MakeAgent(cli.path, where)) {
      agent->remoteHost = host.address;
      agent->iconPath = DefaultIconFor(*agent);
      found.push_back(std::move(*agent));
    }
  }
  return found;
}

} // namespace agentscan
