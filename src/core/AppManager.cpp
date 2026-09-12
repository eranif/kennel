#include "core/AppManager.h"

#include "core/Logger.h"

#include <cstdlib>

AppManager &AppManager::Get() {
  static AppManager instance;
  return instance;
}

void AppManager::Initialize(const AppPaths &paths) {
  if (m_initialized) {
    KLOG_WARN() << "AppManager::Initialize called more than once; ignoring";
    return;
  }
  m_paths = paths;

  // Config: load config.json, writing defaults on first run. Malformed JSON is
  // non-fatal — fall back to built-in defaults so the app still launches.
  m_configStore = std::make_unique<ConfigStore>(paths);
  if (StatusOr<AppConfig> config = m_configStore->Load(); !config.ok()) {
    KLOG_ERROR() << "Could not load config.json: " << config.status().message()
                 << " - continuing with built-in defaults";
    m_config = DefaultConfig();
  } else {
    m_config = config.value();
    m_config.Merge(DefaultConfig());

    for (const wxString &w : m_configStore->Warnings()) {
      KLOG_WARN() << w;
    }
    KLOG_INFO() << "Loaded config.json with " << m_config.agents.size()
                << " agent(s)";
  }

  // Agent/client registry, built from the in-memory config.
  m_adapters = std::make_unique<AdapterRegistry>(m_config);
  KLOG_INFO() << "Registry ready with " << m_adapters->AgentCount()
              << " agent(s)";

  // Workspace store. Corrupt files self-recover (backed up, then empty), so
  // only a hard I/O error is logged. The loaded contents are consumed once by
  // MainView::RestoreSessions() to seed the UI, which is the only source of
  // truth for sessions/groups from then on.
  m_workspaceStore = std::make_unique<WorkspaceStore>(paths);
  if (StatusOr<::Workspace> ws = m_workspaceStore->Load(); !ws.ok()) {
    KLOG_ERROR() << "Could not load workspace.json: " << ws.status().message()
                 << " - starting with an empty workspace";
  } else {
    m_initialWorkspace = ws.value();
    KLOG_INFO() << "Loaded workspace with "
                << static_cast<int>(m_initialWorkspace.sessions.size())
                << " session(s)";
  }

  // Scan OS for shells.
  ::FindShells();

  // UI preferences (non-essential: Load always returns usable values).
  m_uiPrefsStore = std::make_unique<UiPrefsStore>(paths);
  m_uiPrefsStore->Load(m_uiPrefs);

  m_hostsStore = std::make_unique<HostsStore>(paths);

  if (m_uiPrefs.terminalTheme.empty()) {
    m_uiPrefs.terminalTheme = "Cobalt2";
  }

  if (m_uiPrefs.terminalFontDesc.empty()) {
    wxFont font(wxFontInfo(GetDefaultFontSize())
                    .Family(wxFONTFAMILY_TELETYPE)
                    .FaceName(GetDefaultFontFamily()));
    m_uiPrefs.terminalFontDesc = font.GetNativeFontInfoDesc();
  }

  m_uiPrefsStore->Save(m_uiPrefs);
  m_bitmaps = std::make_unique<Bitmaps>();
  m_initialized = true;
}

Status AppManager::Reload() {
  if (!m_configStore) {
    return Status::Error("AppManager not initialized");
  }

  StatusOr<AppConfig> config = m_configStore->Load();
  if (!config.ok()) {
    // Leave the in-memory config/registry intact on a bad file.
    KLOG_ERROR() << "Reload failed; keeping current config: "
                 << config.status().message();
    return config.status();
  }

  m_config = config.value();
  for (const wxString &w : m_configStore->Warnings()) {
    KLOG_WARN() << w;
  }
  m_adapters->Rebuild(m_config);
  KLOG_INFO() << "Reloaded config.json with " << m_config.agents.size()
              << " agent(s)";
  return Status::Ok();
}

const AppPaths &AppManager::Paths() const {
  if (!m_paths) {
    std::abort(); // used before Initialize()
  }
  return *m_paths;
}

AppConfig &AppManager::Config() { return m_config; }
const AppConfig &AppManager::Config() const { return m_config; }

ConfigStore &AppManager::Configs() {
  if (!m_configStore) {
    std::abort();
  }
  return *m_configStore;
}

AdapterRegistry &AppManager::Adapters() {
  if (!m_adapters) {
    std::abort();
  }
  return *m_adapters;
}

WorkspaceStore &AppManager::Workspace() {
  if (!m_workspaceStore) {
    std::abort();
  }
  return *m_workspaceStore;
}

HostsStore &AppManager::Hosts() {
  if (!m_hostsStore) {
    std::abort();
  }
  return *m_hostsStore;
}

UiPrefs &AppManager::GetPrefs() { return m_uiPrefs; }

Status AppManager::SavePrefs() {
  if (!m_uiPrefsStore) {
    return Status::Error("Prefs store not initialised");
  }
  return m_uiPrefsStore->Save(m_uiPrefs);
}

Bitmaps &AppManager::GetBitmaps() {
  if (!m_bitmaps) {
    std::abort();
  }
  return *m_bitmaps;
}
