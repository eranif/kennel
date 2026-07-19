#pragma once

#include <wx/string.h>

#include "core/AdapterRegistry.h"
#include "core/AppPaths.h"
#include "core/Status.h"
#include "core/Workspace.h"
#include "core/WorkspaceStore.h"

#include <vector>

// Inputs for creating a new session.
struct NewSessionRequest {
  wxString name;
  wxString agentName;
  wxString workingDir;
  bool resume{false};
  wxString groupName;
  bool plainTerminal{false};
};

// Owns the in-memory session registry and persistence to workspace.json.
// Nothing throws.
class WorkspaceManager {
public:
  WorkspaceManager(AppPaths paths, WorkspaceStore *store,
                   const AdapterRegistry *registry);

  Status Load();

  const std::vector<Session> &Sessions() const { return m_sessions; }

  const Session *Find(const wxString &name) const;

  // Creates a session after validating a non-empty unique name and a known
  // agent name. Does not persist (call Persist()).
  StatusOr<Session> Create(const NewSessionRequest &req);

  // Renames a session. Does not persist.
  Status Rename(const wxString &from, const wxString &to);

  // Renames a logical group: retags every session whose groupName is `from`
  // to `to`. Rejects an empty target or one that collides with an existing
  // group. Does not persist.
  Status RenameGroup(const wxString &from, const wxString &to);

  // Moves a single session into `toGroup` (created implicitly if it does not
  // yet exist). Rejects an empty target. Does not persist.
  Status MoveSession(const wxString &name, const wxString &toGroup);

  // Removes a session from the registry. Does not persist.
  Status CloseSession(const wxString &name);

  // Removes a session from the registry. Does not persist.
  Status CloseGroup(const wxString &name);

  // Returns the persisted icon alias for `groupName`, or an empty string if
  // none has been assigned yet.
  wxString GroupIcon(const wxString &groupName) const;

  // Assigns (or replaces) the icon alias for `groupName`. Does not persist.
  void SetGroupIcon(const wxString &groupName, const wxString &icon);

  // Writes the current registry to workspace.json.
  Status Persist();

  void CloseAll();

private:
  std::vector<Session>::iterator FindIt(const wxString &name);
  std::vector<GroupMeta>::iterator FindGroupMetaIt(const wxString &name);

  AppPaths m_paths;
  WorkspaceStore *m_store;
  const AdapterRegistry *m_registry;
  std::vector<Session> m_sessions;
  std::vector<GroupMeta> m_groups;
  int m_version = 1;
};
