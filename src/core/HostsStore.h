#pragma once

#include "core/AppPaths.h"
#include "core/SshConfigParser.h"
#include "core/Status.h"

#include <vector>

// Loads and saves hosts.json (user-defined host entries only).
// SSH config entries are parsed separately and are never written here.
class HostsStore {
public:
  explicit HostsStore(AppPaths paths);

  // Loads hosts.json. Missing file => empty list (OK).
  // Malformed JSON => empty list (OK, original file left untouched).
  StatusOr<std::vector<SshHostEntry>> Load();

  // Serializes and writes user hosts to hosts.json (pretty-printed).
  Status Save(const std::vector<SshHostEntry> &hosts);

private:
  AppPaths m_paths;
};
