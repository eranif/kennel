#include "core/lsp/LspManager.h"

#include "core/EventNotifier.hpp"
#include "core/Logger.h"
#include "core/RemoteInstaller.h"
#include "core/Ripgrep.h"

#include <wx/intl.h>

wxDEFINE_EVENT(wxEVT_LSP_INSTALL_NEEDED, LspManagerEvent);
wxDEFINE_EVENT(wxEVT_LSP_SERVER_READY, LspManagerEvent);
wxDEFINE_EVENT(wxEVT_LSP_SERVER_FAILED, LspManagerEvent);

// One server: where it is on its way to being used, the installer that looks
// for its program on the machine (and installs it when the user agrees), and
// its client. It is the handler of the events of both.
class LspManager::Entry : public wxEvtHandler {
public:
  explicit Entry(const LspServerKey &key) : m_key(key), m_installer(this) {
    Bind(wxEVT_INSTALL_DONE, &Entry::OnInstallDone, this);
    Bind(wxEVT_INSTALL_PROGRESS, &Entry::OnInstallProgress, this);
    EventNotifier::Get()->Bind(wxEVT_LSP_CLIENT_READY, &Entry::OnClientReady,
                               this);
    EventNotifier::Get()->Bind(wxEVT_LSP_CLIENT_FAILED, &Entry::OnClientFailed,
                               this);
  }

  ~Entry() override {
    EventNotifier::Get()->Unbind(wxEVT_LSP_CLIENT_READY, &Entry::OnClientReady,
                                 this);
    EventNotifier::Get()->Unbind(wxEVT_LSP_CLIENT_FAILED,
                                 &Entry::OnClientFailed, this);
  }

  // Looks for the program on the machine, and changes nothing there. The
  // answer comes with OnInstallDone().
  void Check() {
    KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": looking for "
                 << m_key.GetTool()->name << " on "
                 << m_key.m_account.GetDisplayName() << " in "
                 << ripgrep::InstallDir();
    const Status started = m_installer.Start(m_key.m_account, *m_key.GetTool(),
                                             ripgrep::InstallDir(),
                                             RemoteInstaller::Mode::kCheckOnly);
    if (!started.ok()) {
      Fail(started.message());
    }
  }

  // Installs the program (the user agreed).
  void Install() {
    KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": installing "
                 << m_key.GetTool()->name << " " << m_key.GetTool()->version
                 << " on " << m_key.m_account.GetDisplayName();
    m_state = LspServerState::kInstalling;
    const Status started = m_installer.Start(m_key.m_account, *m_key.GetTool(),
                                             ripgrep::InstallDir(),
                                             RemoteInstaller::Mode::kInstall);
    if (!started.ok()) {
      Fail(started.message());
    }
  }

  LspServerState m_state{LspServerState::kChecking};
  // Made when the program is there (from kInitializing); usable when the state
  // is kReady.
  std::shared_ptr<LspClient> m_client;
  // Set when the state is kFailed.
  wxString m_message;

private:
  void OnInstallProgress(InstallEvent &event) {
    KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": " << event.GetString();
  }

  void OnInstallDone(InstallEvent &event) {
    if (event.IsCancelled()) {
      KLOG_DEBUG() << "LSP | " << m_key.GetKey()
                   << ": the install was cancelled";
      return;
    }
    if (!event.GetResult().ok()) {
      Fail(event.GetResult().message());
      return;
    }
    const ToolDescription &tool = *m_key.GetTool();
    if (event.NeedsInstall()) {
      KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": " << tool.name
                   << " is not there, asking the user";
      m_state = LspServerState::kAwaitingConsent;
      Send(wxEVT_LSP_INSTALL_NEEDED, tool.name + " " + tool.version);
      return;
    }
    KLOG_INFO() << "LSP | " << tool.name << " " << tool.version << " is on "
                << m_key.m_account.GetDisplayName() << " at "
                << event.GetPath();
    Launch(event.GetPath());
  }

  // Starts the server and the handshake. The answer comes with OnClientReady()
  // or OnClientFailed().
  void Launch(const wxString &installedPath) {
    KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": launching "
                 << m_key.GetProgram(installedPath);
    m_state = LspServerState::kInitializing;
    m_client =
        std::make_shared<LspClient>(m_key, m_key.GetProgram(installedPath));
    const Status started = m_client->Start();
    if (!started.ok()) {
      m_client.reset();
      Fail(started.message());
    }
  }

  void OnClientReady(wxCommandEvent &event) {
    if (event.GetEventObject() != m_client.get()) {
      event.Skip(); // The client of another entry
      return;
    }
    KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": the handshake is done";
    m_state = LspServerState::kReady;
    Send(wxEVT_LSP_SERVER_READY, wxEmptyString);
  }

  void OnClientFailed(wxCommandEvent &event) {
    if (event.GetEventObject() != m_client.get()) {
      event.Skip();
      return;
    }
    m_client.reset();
    Fail(event.GetString());
  }

  void Fail(const wxString &message) {
    KLOG_WARN() << "LSP | " << message;
    m_state = LspServerState::kFailed;
    m_message = message;
    Send(wxEVT_LSP_SERVER_FAILED, message);
  }

  void Send(wxEventType type, const wxString &text) {
    LspManagerEvent event(type);
    event.SetKey(m_key);
    event.SetString(text);
    EventNotifier::Get()->AddPendingEvent(event);
  }

  LspServerKey m_key;
  RemoteInstaller m_installer;
};

LspManager &LspManager::Get() {
  static LspManager manager;
  return manager;
}

LspManager::~LspManager() = default;

StatusOr<std::shared_ptr<LspClient>>
LspManager::GetServerFor(const LspServerKey &key) {
  if (key.GetTool() == nullptr) {
    return Status::Error(
        StatusCode::kNotAvailable,
        _("There is no language server to install for this language."));
  }

  const wxString name = key.m_account.GetDisplayName();
  auto [it, added] = m_entries.try_emplace(key.GetKey());
  if (added) {
    KLOG_DEBUG() << "LSP | " << key.GetKey() << ": a new server is wanted";
    it->second = std::make_unique<Entry>(key);
    it->second->Check();
  }

  const Entry &entry = *it->second;
  switch (entry.m_state) {
  case LspServerState::kReady:
    return entry.m_client;
  case LspServerState::kDeclined:
    return Status::Error(
        StatusCode::kPermissionDenied,
        wxString::Format(_("The language server is not installed on %s."),
                         name));
  case LspServerState::kFailed:
    return Status::Error(entry.m_message);
  case LspServerState::kChecking:
  case LspServerState::kAwaitingConsent:
  case LspServerState::kInstalling:
  case LspServerState::kInitializing:
    break;
  }
  return Status::Error(
      StatusCode::kUnavailable,
      wxString::Format(_("The language server on %s is not ready yet."), name));
}

void LspManager::Shutdown() {
  KLOG_DEBUG() << "LSP | shutting down, " << m_entries.size() << " server(s)";
  while (!m_entries.empty()) {
    auto it = m_entries.begin();
    KLOG_DEBUG() << "LSP | " << it->first << ": ending it";
    m_entries.erase(it);
    KLOG_DEBUG() << "LSP | ended";
  }
}

void LspManager::Install(const LspServerKey &key) {
  auto it = m_entries.find(key.GetKey());
  if (it != m_entries.end() &&
      it->second->m_state == LspServerState::kAwaitingConsent) {
    KLOG_DEBUG() << "LSP | " << key.GetKey() << ": the user agreed to install";
    it->second->Install();
  }
}

void LspManager::StopUnused(const std::set<wxString> &neededKeys) {
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    if (neededKeys.count(it->first) != 0 ||
        it->second->m_state == LspServerState::kDeclined) {
      ++it;
      continue;
    }
    KLOG_DEBUG() << "LSP | " << it->first << ": no page needs it, ending it";
    it = m_entries.erase(it);
  }
}

void LspManager::Decline(const LspServerKey &key) {
  auto it = m_entries.find(key.GetKey());
  if (it != m_entries.end() &&
      it->second->m_state == LspServerState::kAwaitingConsent) {
    KLOG_DEBUG() << "LSP | " << key.GetKey()
                 << ": the user declined the install";
    it->second->m_state = LspServerState::kDeclined;
  }
}
