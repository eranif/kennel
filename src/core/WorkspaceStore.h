#pragma once

#include "core/AppPaths.h"
#include "core/Status.h"
#include "core/Workspace.h"

// Loads and saves workspace.json (the single implicit workspace + sessions).
// Recovery-oriented: a missing file yields an empty workspace, and a corrupt
// file is backed up (workspace.json.bak-<ts>) before an empty workspace is
// returned, so a relaunch never loses the file silently. Nothing throws.
class WorkspaceStore {
public:
  explicit WorkspaceStore(AppPaths paths);

  // Loads workspace.json. Missing file => empty workspace (OK). Malformed
  // JSON => the file is backed up and an empty workspace is returned (OK);
  // an I/O failure (read/backup) is surfaced as an error status.
  StatusOr<Workspace> Load();

  // Serializes and writes the workspace to workspace.json (pretty-printed).
  Status Save(const Workspace &);

private:
  AppPaths m_paths;
};
