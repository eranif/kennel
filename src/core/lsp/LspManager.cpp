#include "core/lsp/LspManager.h"

#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Logger.h"
#include "core/RemoteInstaller.h"
#include "core/Ripgrep.h"

#include <algorithm>
#include <chrono>
#include <wx/intl.h>

namespace {
// How often the timer of the manager looks at the processes of the servers.
constexpr int kWatchIntervalMs = 1000;
} // namespace

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

  // Called by the timer of the manager, once a second: a server whose process
  // ended is started again after a delay (see StartAgainLater()), and a server
  // that waits for its delay is started when it is over.
  void Watch() {
    const Clock::time_point now = Clock::now();
    switch (m_state) {
    case LspServerState::kInitializing:
    case LspServerState::kReady:
      if (m_client && !m_client->IsRunning()) {
        OnProcessEnded(now);
      }
      break;
    case LspServerState::kRestarting:
      if (now >= m_restartAt) {
        Launch();
      }
      break;
    case LspServerState::kChecking:
    case LspServerState::kAwaitingConsent:
    case LspServerState::kInstalling:
    case LspServerState::kDeclined:
    case LspServerState::kFailed:
      break;
    }
  }

  LspServerState m_state{LspServerState::kChecking};
  // Made when the program is there (from kInitializing); usable when the state
  // is kReady.
  std::shared_ptr<LspClient> m_client;
  // Set when the state is kFailed.
  wxString m_message;

private:
  using Clock = std::chrono::steady_clock;

  // The delay before the first restart; it doubles with every failure in a row,
  // up to kMaxDelay.
  static constexpr std::chrono::seconds kFirstDelay{1};
  static constexpr std::chrono::seconds kMaxDelay{60};
  // The server gives up (kFailed) after this many failures in a row.
  static constexpr int kMaxFailures = 5;
  // A server that ran this long was healthy: the failures start from 0 again.
  static constexpr std::chrono::seconds kHealthyUptime{60};

  // The process of the server ended on its own.
  void OnProcessEnded(const Clock::time_point &now) {
    const auto uptime =
        std::chrono::duration_cast<std::chrono::seconds>(now - m_launchedAt);
    KLOG_DEBUG() << "LSP | " << m_key.GetKey()
                 << ": the process ended, it ran for " << uptime.count() << "s";
    m_client.reset();
    if (uptime >= kHealthyUptime) {
      m_failures = 0;
    }
    ++m_failures;
    if (m_failures > kMaxFailures) {
      Fail(wxString::Format(_("The language server ended %d times in a row; "
                              "giving up."),
                            m_failures));
      return;
    }
    // 1 s, 2 s, 4 s ... at most kMaxDelay.
    std::chrono::seconds delay = kFirstDelay;
    for (int i = 1; i < m_failures && delay < kMaxDelay; ++i) {
      delay *= 2;
    }
    delay = std::min(delay, kMaxDelay);
    KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": failure " << m_failures
                 << " of " << kMaxFailures << ", starting it again in "
                 << delay.count() << "s";
    m_restartAt = now + delay;
    m_state = LspServerState::kRestarting;
  }

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
      // The user said No before, and asked not to be asked again.
      const std::optional<bool> stored =
          AppManager::Get().GetDialogAnswers().FindAnswer(
              m_key.GetInstallAnswerKey());
      if (stored && !*stored) {
        KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": " << tool.name
                     << " is not there, and the answer is stored: No";
        m_state = LspServerState::kDeclined;
        return;
      }
      KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": " << tool.name
                   << " is not there, asking the user";
      m_state = LspServerState::kAwaitingConsent;
      Send(wxEVT_LSP_INSTALL_NEEDED, tool.name + " " + tool.version);
      return;
    }
    KLOG_INFO() << "LSP | " << tool.name << " " << tool.version << " is on "
                << m_key.m_account.GetDisplayName() << " at "
                << event.GetPath();
    m_installedPath = event.GetPath();
    Launch();
  }

  // Starts the server and the handshake. The answer comes with OnClientReady()
  // or OnClientFailed().
  void Launch() {
    KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": launching "
                 << m_key.GetProgram(m_installedPath);
    m_state = LspServerState::kInitializing;
    m_launchedAt = Clock::now();
    m_client =
        std::make_shared<LspClient>(m_key, m_key.GetProgram(m_installedPath));
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
  // Where RemoteInstaller found (or put) the program, as it reports it.
  wxString m_installedPath;
  // When the process was started last.
  Clock::time_point m_launchedAt;
  // When to start it again (kRestarting).
  Clock::time_point m_restartAt;
  // How many times in a row the process ended.
  int m_failures{0};
};

LspManager &LspManager::Get() {
  static LspManager manager;
  return manager;
}

LspManager::~LspManager() = default;

void LspManager::OnTimer(wxTimerEvent &) {
  for (auto &[key, entry] : m_entries) {
    entry->Watch();
  }
}

StatusOr<std::shared_ptr<LspClient>>
LspManager::GetServerFor(const LspServerKey &key) {
  if (m_isShutdown) {
    return Status::Error(
        StatusCode::kUnavailable,
        _("The language servers are shut down: Kennel is closing."));
  }
  if (key.GetTool() == nullptr) {
    return Status::Error(
        StatusCode::kNotAvailable,
        _("There is no language server to install for this language."));
  }

  // Declined means that the answer is stored. The user took it back in the
  // Settings: the question is open again.
  if (const auto declined = m_entries.find(key.GetKey());
      declined != m_entries.end() &&
      declined->second->m_state == LspServerState::kDeclined &&
      !AppManager::Get().GetDialogAnswers().FindAnswer(
          key.GetInstallAnswerKey())) {
    KLOG_DEBUG() << "LSP | " << key.GetKey()
                 << ": the stored answer was removed, asking again";
    m_entries.erase(declined);
  }

  const wxString name = key.m_account.GetDisplayName();
  auto [it, added] = m_entries.try_emplace(key.GetKey());
  if (added) {
    KLOG_DEBUG() << "LSP | " << key.GetKey() << ": a new server is wanted";
    it->second = std::make_unique<Entry>(key);
    it->second->Check();
    if (!m_timer) {
      // One timer for all the servers.
      m_timer = std::make_unique<wxTimer>(this);
      Bind(wxEVT_TIMER, &LspManager::OnTimer, this);
      m_timer->Start(kWatchIntervalMs);
      KLOG_DEBUG() << "LSP | watching the servers every " << kWatchIntervalMs
                   << " ms";
    }
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
  case LspServerState::kRestarting:
    break;
  }
  return Status::Error(
      StatusCode::kUnavailable,
      wxString::Format(_("The language server on %s is not ready yet."), name));
}

void LspManager::Shutdown() {
  m_isShutdown = true;
  // First the timer: nothing may start a server again.
  if (m_timer) {
    m_timer->Stop();
    m_timer.reset();
    Unbind(wxEVT_TIMER, &LspManager::OnTimer, this);
  }
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
    // Not asked again, on this machine, until the user takes the answer back.
    const Status stored = AppManager::Get().GetDialogAnswers().Store(
        key.GetInstallAnswerKey(), key.GetInstallQuestion(), false);
    if (!stored.ok()) {
      KLOG_WARN() << "LSP | could not store the answer: " << stored.message();
    }
  }
}
