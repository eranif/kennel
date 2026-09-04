#include "core/ConfigStore.h"

#include "core/JsonUtil.h"

#include <string>

namespace {

using jsonutil::FromUtf8;
using jsonutil::GetBool;
using jsonutil::GetInt;
using jsonutil::GetStr;
using jsonutil::GetStrArray;
using jsonutil::ReadFileUtf8;
using jsonutil::ToUtf8;
using jsonutil::WriteFileUtf8;
using nlohmann::json;

json ToJson(const AppConfig &cfg) {
  json agents = json::array();
  for (const AgentDef &a : cfg.agents) {
    json baseArgs = json::array();
    for (const auto &arg : a.baseArgs)
      baseArgs.push_back(ToUtf8(arg));

    json extraArgs = json::array();
    for (const auto &arg : a.extraArgs)
      extraArgs.push_back(ToUtf8(arg));

    json env = json::object();
    for (const auto &[k, v] : a.env)
      env[ToUtf8(k)] = ToUtf8(v);

    agents.push_back({
        {"name", ToUtf8(a.name)},
        {"executable", ToUtf8(a.executable)},
        {"baseArgs", baseArgs},
        {"resumeArg", ToUtf8(a.resumeArg)},
        {"nonInteractiveArg", ToUtf8(a.nonInteractiveArg)},
        {"iconPath", ToUtf8(a.iconPath)},
        {"extraArgs", extraArgs},
        {"env", env},
        {"remoteHost", ToUtf8(a.remoteHost)},
        {"remoteUser", ToUtf8(a.remoteUser)},
        {"loginShell", ToUtf8(a.loginShell)},
    });
  }

  json jobs = json::array();
  for (const JobDef &j : cfg.jobs) {
    jobs.push_back({
        {"name", ToUtf8(j.name)},
        {"type", ToUtf8(JobTypeToString(j.type))},
        {"command", ToUtf8(j.command)},
        {"agentName", ToUtf8(j.agentName)},
        {"prompt", ToUtf8(j.prompt)},
        {"intervalHours", j.intervalHours},
        {"keepTerminalOpen", j.keepTerminalOpen},
        {"enabled", j.enabled},
    });
  }

  return json{
      {"version", cfg.version},
      {"global", {}},
      {"agents", agents},
      {"jobs", jobs},
  };
}

GlobalSettings ParseGlobal(const json &g) {
  wxUnusedVar(g);
  GlobalSettings out;
  return out;
}

// Backward compatibility: agents configured before AgentDef::nonInteractiveArg
// existed have no value for it. Fill in a sensible default for the known
// built-in CLIs so existing configs work with Prompt jobs without the user
// having to revisit every agent by hand.
wxString DefaultNonInteractiveArg(const wxString &executable) {
  const wxString exeName = wxFileName(executable).GetName().Lower();
  if (exeName == "claude") {
    return "-p";
  }
  if (exeName == "codex") {
    return "exec";
  }
  if (exeName == "kiro-cli") {
    return "chat --no-interactive";
  }
  return wxEmptyString;
}

AgentDef ParseAgent(const json &j) {
  AgentDef out;
  out.name = GetStr(j, "name");
  out.executable = GetStr(j, "executable");
  out.baseArgs = GetStrArray(j, "baseArgs");
  out.resumeArg = GetStr(j, "resumeArg");
  out.nonInteractiveArg = GetStr(j, "nonInteractiveArg");
  if (out.nonInteractiveArg.empty()) {
    out.nonInteractiveArg = DefaultNonInteractiveArg(out.executable);
  }
  out.iconPath = GetStr(j, "iconPath");
  out.extraArgs = GetStrArray(j, "extraArgs");
  out.remoteHost = GetStr(j, "remoteHost");
  out.remoteUser = GetStr(j, "remoteUser");
  out.loginShell = GetStr(j, "loginShell");
  if (auto e = j.find("env"); e != j.end() && e->is_object()) {
    for (auto it = e->begin(); it != e->end(); ++it) {
      if (it.value().is_string()) {
        out.env[FromUtf8(it.key())] = FromUtf8(it.value().get<std::string>());
      }
    }
  }
  return out;
}

JobDef ParseJob(const json &j) {
  JobDef out;
  out.name = GetStr(j, "name");
  out.type = JobTypeFromString(GetStr(j, "type"));
  out.command = GetStr(j, "command");
  out.agentName = GetStr(j, "agentName");
  out.prompt = GetStr(j, "prompt");
  out.intervalHours = GetInt(j, "intervalHours", 1);
  out.keepTerminalOpen = GetBool(j, "keepTerminalOpen", true);
  out.enabled = GetBool(j, "enabled", true);
  return out;
}

AppConfig ParseConfig(const json &root) {
  AppConfig cfg;
  cfg.version = GetInt(root, "version", 1);
  if (auto it = root.find("global"); it != root.end() && it->is_object()) {
    cfg.global = ParseGlobal(*it);
  }
  if (auto it = root.find("agents"); it != root.end() && it->is_array()) {
    for (const auto &a : *it) {
      if (a.is_object()) {
        cfg.agents.push_back(ParseAgent(a));
      }
    }
  }
  if (auto it = root.find("jobs"); it != root.end() && it->is_array()) {
    for (const auto &j : *it) {
      if (j.is_object()) {
        cfg.jobs.push_back(ParseJob(j));
      }
    }
  }
  return cfg;
}

} // namespace

ConfigStore::ConfigStore(AppPaths paths) : m_paths(std::move(paths)) {}

StatusOr<AppConfig> ConfigStore::Load() {
  m_warnings.clear();
  const wxString path = m_paths.ConfigFile().GetFullPath();

  if (!wxFileExists(path)) {
    AppConfig def = DefaultConfig();
    Status s = Save(def);
    if (!s.ok()) {
      return s;
    }
    return def;
  }

  std::string raw;
  if (!ReadFileUtf8(path, &raw)) {
    return Status::Error(
        wxString::Format("Could not read config file: %s", path));
  }

  json root = json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    return Status::Error(
        wxString::Format("Invalid JSON in config file: %s", path));
  }

  return ParseConfig(root);
}

Status ConfigStore::Save(const AppConfig &cfg) {
  const wxString path = m_paths.ConfigFile().GetFullPath();
  const std::string text = ToJson(cfg).dump(2);
  if (!WriteFileUtf8(path, text)) {
    return Status::Error(
        wxString::Format("Could not write config file: %s", path));
  }
  return Status::Ok();
}

StatusOr<wxString> ConfigStore::RawText() {
  const wxString path = m_paths.ConfigFile().GetFullPath();
  if (!wxFileExists(path)) {
    return wxString();
  }
  std::string raw;
  if (!ReadFileUtf8(path, &raw)) {
    return Status::Error(
        wxString::Format("Could not read config file: %s", path));
  }
  return FromUtf8(raw);
}

Status ConfigStore::SaveRawText(const wxString &text) {
  const std::string raw = ToUtf8(text);
  json parsed = json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded()) {
    return Status::Error("Invalid JSON: refusing to save config.");
  }
  const wxString path = m_paths.ConfigFile().GetFullPath();
  if (!WriteFileUtf8(path, raw)) {
    return Status::Error(
        wxString::Format("Could not write config file: %s", path));
  }
  return Status::Ok();
}
