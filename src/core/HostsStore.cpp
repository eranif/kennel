#include "core/HostsStore.h"

#include "core/JsonUtil.h"
#include "core/Logger.h"

namespace {

using jsonutil::GetStr;
using jsonutil::ReadFileUtf8;
using jsonutil::ToUtf8;
using jsonutil::WriteFileUtf8;
using nlohmann::json;

json ToJson(const std::vector<SshHostEntry> &hosts) {
  json arr = json::array();
  for (const SshHostEntry &h : hosts) {
    arr.push_back({{"name", ToUtf8(h.name)}, {"address", ToUtf8(h.address)}});
  }
  return json{{"version", 1}, {"hosts", arr}};
}

std::vector<SshHostEntry> ParseHosts(const json &root) {
  std::vector<SshHostEntry> out;
  auto it = root.find("hosts");
  if (it == root.end() || !it->is_array()) {
    return out;
  }
  for (const auto &h : *it) {
    if (!h.is_object()) {
      continue;
    }
    SshHostEntry e;
    e.name = GetStr(h, "name");
    e.address = GetStr(h, "address");
    if (!e.name.empty()) {
      out.push_back(std::move(e));
    }
  }
  return out;
}

} // namespace

HostsStore::HostsStore(AppPaths paths) : m_paths(std::move(paths)) {}

StatusOr<std::vector<SshHostEntry>> HostsStore::Load() {
  const wxString path = m_paths.HostsFile().GetFullPath();
  if (!wxFileExists(path)) {
    return std::vector<SshHostEntry>{};
  }

  std::string raw;
  if (!ReadFileUtf8(path, &raw)) {
    return Status::Error(
        wxString::Format("Could not read hosts file: %s", path));
  }

  json root = json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    KLOG_WARN() << "hosts.json is malformed; ignoring";
    return std::vector<SshHostEntry>{};
  }

  return ParseHosts(root);
}

Status HostsStore::Save(const std::vector<SshHostEntry> &hosts) {
  const wxString path = m_paths.HostsFile().GetFullPath();
  const std::string text = ToJson(hosts).dump(2);
  if (!WriteFileUtf8(path, text)) {
    return Status::Error(
        wxString::Format("Could not write hosts file: %s", path));
  }
  return Status::Ok();
}
