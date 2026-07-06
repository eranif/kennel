#include "core/WorkspaceStore.h"

#include <wx/datetime.h>
#include <wx/filefn.h>

#include "core/JsonUtil.h"
#include "core/Logger.h"

namespace {

using jsonutil::GetInt;
using jsonutil::GetStr;
using jsonutil::ReadFileUtf8;
using jsonutil::ToUtf8;
using jsonutil::WriteFileUtf8;
using nlohmann::json;

json ToJson(const Workspace &ws) {
  json sessions = json::array();
  for (const Session &s : ws.sessions) {
    if (s.plainTerminal)
      // do not persist terminals
      continue;
    sessions.push_back({
        {"name", ToUtf8(s.name)},
        {"agentName", ToUtf8(s.agentName)},
        {"workingDir", ToUtf8(s.workingDir)},
        {"groupName", ToUtf8(s.groupName)},
    });
  }
  return json{
      {"version", ws.version},
      {"workspace", {{"sessions", sessions}}},
  };
}

Session ParseSession(const json &j) {
  Session s;
  s.name = GetStr(j, "name");
  s.agentName = GetStr(j, "agentName");
  s.workingDir = GetStr(j, "workingDir");
  s.groupName = GetStr(j, "groupName", _("Default"));
  return s;
}

Workspace ParseWorkspace(const json &root) {
  Workspace ws;
  ws.version = GetInt(root, "version", 1);
  if (auto wsIt = root.find("workspace");
      wsIt != root.end() && wsIt->is_object()) {
    if (auto sIt = wsIt->find("sessions");
        sIt != wsIt->end() && sIt->is_array()) {
      for (const auto &s : *sIt) {
        if (s.is_object()) {
          ws.sessions.push_back(ParseSession(s));
        }
      }
    }
  }
  return ws;
}

bool BackupCorruptFile(const wxString &path) {
  wxDateTime now = wxDateTime::UNow();
  const wxString backup = path + ".bak-" + now.Format("%Y%m%d-%H%M%S");
  KLOG_WARN() << "workspace.json is corrupt; backing up to " << backup;
  return wxRenameFile(path, backup, /*overwrite=*/false);
}

} // namespace

WorkspaceStore::WorkspaceStore(AppPaths paths) : m_paths(std::move(paths)) {}

StatusOr<Workspace> WorkspaceStore::Load() {
  const wxString path = m_paths.WorkspaceFile().GetFullPath();

  if (!wxFileExists(path)) {
    return Workspace{};
  }

  std::string raw;
  if (!ReadFileUtf8(path, &raw)) {
    return Status::Error(
        wxString::Format("Could not read workspace file: %s", path));
  }

  json root = json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    if (!BackupCorruptFile(path)) {
      return Status::Error(wxString::Format(
          "workspace.json is corrupt and could not be backed up: %s", path));
    }
    return Workspace{};
  }

  return ParseWorkspace(root);
}

Status WorkspaceStore::Save(const Workspace &ws) {
  const wxString path = m_paths.WorkspaceFile().GetFullPath();
  const std::string text = ToJson(ws).dump(2);
  if (!WriteFileUtf8(path, text)) {
    return Status::Error(
        wxString::Format("Could not write workspace file: %s", path));
  }
  return Status::Ok();
}
