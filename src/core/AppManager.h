#pragma once

#include "core/AdapterRegistry.h"
#include "core/AppPaths.h"
#include "core/Bitmaps.h"
#include "core/Config.h"
#include "core/ConfigStore.h"
#include "core/HostsStore.h"
#include "core/Status.h"
#include "core/UiPrefs.h"
#include "core/UiPrefsStore.h"
#include "core/Workspace.h"
#include "core/WorkspaceManager.h"
#include "core/WorkspaceStore.h"

#include <memory>
#include <optional>

// Process-wide owner of the application's configuration and persistence
// objects (paths, config + its store, adapter registry, workspace manager + its
// store, UI prefs + its store). Lets UI code reach shared state via
// AppManager::Get() instead of threading a dependency through every
// constructor. GUI-free, so it lives in kennel_core.
//
// Lifetime: call Initialize() exactly once at startup (after the data dir and
// logger are ready). Accessors abort if used before Initialize().
class AppManager {
public:
  static AppManager &Get();

  // Builds the config/workspace/prefs objects rooted at `paths`:
  //   - loads config.json (falls back to DefaultConfig on error),
  //   - builds the adapter registry,
  //   - loads workspace.json through the manager,
  //   - loads .persist.json (UI prefs).
  // Idempotent guard: a second call is ignored (logged). Never throws.
  void Initialize(const AppPaths &paths);
  bool IsInitialized() const { return m_initialized; }

  // Re-reads config.json from disk into the in-memory AppConfig and rebuilds
  // the adapter registry (e.g. after the in-app config editor saves). On a
  // malformed file the in-memory config/registry are left unchanged and an
  // error Status is returned. Warnings from the reload are logged.
  //
  // WARNING: rebuilding the registry destroys the previous ClientAdapter
  // objects, so any live SessionPage still holding a ClientAdapter* would
  // dangle. Only call this when no session is running against the old registry
  // (or treat adapter changes as "apply on restart"). Workspace and UI prefs
  // are NOT reloaded (they hold live in-memory state).
  Status Reload();

  const AppPaths &Paths() const;

  Bitmaps &GetBitmaps();

  // The in-memory config and the store backing config.json.
  AppConfig &Config();
  const AppConfig &Config() const;
  ConfigStore &Configs();

  AdapterRegistry &Adapters();
  WorkspaceManager &Workspace();
  HostsStore &Hosts();

  // Distinct logical group names currently in use, collected O(n) from the
  // workspace sessions. Always includes the implicit "Default" group. Order:
  // "Default" first, then remaining groups in first-seen order.
  wxArrayString Groups() const;

  // UI preferences: the live snapshot and the store backing .persist.json.
  UiPrefs &GetPrefs();
  Status SavePrefs();

private:
  AppManager() = default;

  bool m_initialized = false;
  std::optional<AppPaths> m_paths;
  AppConfig m_config;
  UiPrefs m_uiPrefs;
  std::unique_ptr<ConfigStore> m_configStore;
  std::unique_ptr<AdapterRegistry> m_adapters;
  std::unique_ptr<WorkspaceStore> m_workspaceStore;
  std::unique_ptr<WorkspaceManager> m_workspace;
  std::unique_ptr<UiPrefsStore> m_uiPrefsStore;
  std::unique_ptr<HostsStore> m_hostsStore;
  std::unique_ptr<Bitmaps> m_bitmaps;
};
