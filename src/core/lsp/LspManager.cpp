#include "core/lsp/LspManager.h"

#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Globals.hpp"
#include "core/Helpers.h"
#include "core/IManager.h"
#include "core/Logger.h"
#include "core/RemoteInstaller.h"
#include "core/Ripgrep.h"

#include <algorithm>
#include <chrono>
#include <utility>
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

  // Looks for the program on the machine, and changes nothing there. On this
  // machine a program that is installed already is launched at once, otherwise
  // the answer comes with OnInstallDone().
  void Check() {
    // This machine: a server that is installed already is the one to use (it
    // knows where the headers of its compiler are). Nothing is asked or
    // installed then, and the version is not looked at. The other machines have
    // the install flow only.
    const bool isLocal = m_key.m_account.IsLocalHost();
    if (isLocal) {
      if (const std::optional<wxString> found =
              platform::Which(m_key.GetBinaryName())) {
        KLOG_INFO() << "LSP | " << m_key.GetKey() << ": using the installed "
                    << m_key.GetKindName() << " at " << *found;
        m_installedPath = *found;
        Launch();
        return;
      }
    }
    if (m_key.GetTool() == nullptr) {
      // A server that Kennel does not install, and is not on this machine.
      if (!isLocal) {
        // Only this machine is looked at for now (a WSL distribution or a host
        // would need a shell of its own to look in).
        Fail(wxString::Format(
            _("%s is only looked for on this machine, not on %s."),
            m_key.GetKindName(), m_key.m_account.GetDisplayName()));
        return;
      }
      KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": "
                   << m_key.GetBinaryName() << " was not found";
      AskTheUser();
      return;
    }
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

  const LspServerKey &GetServerKey() const { return m_key; }

  LspServerState m_state{LspServerState::kChecking};
  // Set when the state is kDeclined: the answer is in the stored answers (the
  // user asked to remember it), so that taking it back there asks again. A
  // decline that is not remembered lasts until Kennel ends.
  bool m_declineStored{false};
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
    // The editors that had files on it register again with the next server.
    m_client->NotifyTerminated();
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
      AskTheUser();
      return;
    }
    KLOG_INFO() << "LSP | " << tool.name << " " << tool.version << " is on "
                << m_key.m_account.GetDisplayName() << " at "
                << event.GetPath();
    m_installedPath = event.GetPath();
    Launch();
  }

  // The server is not on the machine. The user answered before and asked for it
  // to be remembered: that answer is used (install, or not). Otherwise the user
  // is asked (wxEVT_LSP_INSTALL_NEEDED); the answer comes with
  // LspManager::Install() or Decline().
  void AskTheUser() {
    const std::optional<bool> stored =
        AppManager::Get().GetDialogAnswers().FindAnswer(
            m_key.GetInstallAnswerKey());
    const ToolDescription *tool = m_key.GetTool();
    if (stored && *stored && tool != nullptr) {
      KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": " << tool->name
                   << " is not there, and the answer is stored: Yes";
      Install();
      return;
    }
    if (stored) {
      KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": "
                   << m_key.GetBinaryName()
                   << " is not there, and the answer is stored: No";
      m_state = LspServerState::kDeclined;
      m_declineStored = true;
      return;
    }
    KLOG_DEBUG() << "LSP | " << m_key.GetKey() << ": " << m_key.GetBinaryName()
                 << " is not there, asking the user";
    m_state = LspServerState::kAwaitingConsent;
    // What the user is told: the program and its version, or how to install it.
    Send(wxEVT_LSP_INSTALL_NEEDED, tool != nullptr
                                       ? tool->name + " " + tool->version
                                       : m_key.GetInstallHint());
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
LspManager::GetClientFor(const LspServerKey &key) {
  if (m_isShutdown) {
    return Status::Error(
        StatusCode::kUnavailable,
        _("The language servers are shut down: Kennel is closing."));
  }
  // A declined answer that was remembered is in the stored answers. The user
  // took it back in the Settings: the question is open again. (One that was not
  // remembered stays declined until Kennel ends.)
  if (const auto declined = m_entries.find(key.GetKey());
      declined != m_entries.end() &&
      declined->second->m_state == LspServerState::kDeclined &&
      declined->second->m_declineStored &&
      !AppManager::Get().GetDialogAnswers().FindAnswer(
          key.GetInstallAnswerKey())) {
    KLOG_DEBUG() << "LSP | " << key.GetKey()
                 << ": the stored answer was removed, asking again";
    m_entries.erase(declined);
  }

  // A server that Kennel does not install: the user was told how to install it.
  // When it is on the machine after that, it is used, without a restart.
  if (key.GetTool() == nullptr && key.m_account.IsLocalHost()) {
    if (const auto notFound = m_entries.find(key.GetKey());
        notFound != m_entries.end() &&
        notFound->second->m_state == LspServerState::kDeclined &&
        platform::Which(key.GetBinaryName())) {
      KLOG_DEBUG() << "LSP | " << key.GetKey() << ": " << key.GetBinaryName()
                   << " is on the machine now";
      m_entries.erase(notFound);
    }
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
  auto *notifier = EventNotifier::Get();
  notifier->Unbind(wxEVT_PAGE_ADDED, &LspManager::OnPageAdded, this);
  notifier->Unbind(wxEVT_PAGE_REMOVED, &LspManager::OnPageRemoved, this);
  notifier->Unbind(wxEVT_PAGE_RENAMED, &LspManager::OnPageRenamed, this);
  notifier->Unbind(wxEVT_PAGE_MOVED, &LspManager::OnPageMoved, this);
  notifier->Unbind(wxEVT_SESSION_PAGES_CLEARED, &LspManager::OnPagesCleared,
                   this);
  // First the timer: nothing may start a server again.
  if (m_timer) {
    m_timer->Stop();
    m_timer.reset();
    Unbind(wxEVT_TIMER, &LspManager::OnTimer, this);
  }
  notifier->Unbind(wxEVT_FILE_SAVED, &LspManager::OnFileSaved, this);
  KLOG_DEBUG() << "LSP | shutting down, " << m_entries.size() << " server(s)";
  while (!m_entries.empty()) {
    auto it = m_entries.begin();
    KLOG_DEBUG() << "LSP | " << it->first << ": ending it";
    m_entries.erase(it);
    KLOG_DEBUG() << "LSP | ended";
  }
}

void LspManager::Install(const LspServerKey &key, bool remember) {
  auto it = m_entries.find(key.GetKey());
  if (it != m_entries.end() &&
      it->second->m_state == LspServerState::kAwaitingConsent) {
    KLOG_DEBUG() << "LSP | " << key.GetKey() << ": the user agreed to install"
                 << (remember ? " (remembered)" : "");
    if (remember) {
      // Not asked again, on this machine, until the user takes the answer back.
      const Status stored = AppManager::Get().GetDialogAnswers().Store(
          key.GetInstallAnswerKey(), key.GetInstallQuestion(), true);
      if (!stored.ok()) {
        KLOG_WARN() << "LSP | could not store the answer: " << stored.message();
      }
    }
    it->second->Install();
  }
}

void LspManager::Initialize() {
  auto *notifier = EventNotifier::Get();
  notifier->Bind(wxEVT_PAGE_ADDED, &LspManager::OnPageAdded, this);
  notifier->Bind(wxEVT_PAGE_REMOVED, &LspManager::OnPageRemoved, this);
  notifier->Bind(wxEVT_PAGE_RENAMED, &LspManager::OnPageRenamed, this);
  notifier->Bind(wxEVT_PAGE_MOVED, &LspManager::OnPageMoved, this);
  notifier->Bind(wxEVT_SESSION_PAGES_CLEARED, &LspManager::OnPagesCleared,
                 this);
  notifier->Bind(wxEVT_FILE_SAVED, &LspManager::OnFileSaved, this);
}

void LspManager::OnFileSaved(FileEvent &event) {
  event.Skip(); // The views of the files follow it too
  IManager *manager = GetKennelManager();
  IEditor *editor =
      manager ? manager->FindEditorByUID(event.GetEditorUID()) : nullptr;
  if (editor == nullptr) {
    return;
  }
  if (const auto client = FindClient(*editor)) {
    client->OnDocumentSaved(editor);
  }
}

bool LspManager::IsClientAvailableFor(IEditor *editor) {
  if (m_isShutdown || editor == nullptr) {
    return false;
  }
  const std::optional<LspServerKey> key = editor->GetServerKey();
  if (!key) {
    return false;
  }
  const auto client = GetClientFor(*key);
  // Not ready yet is on its way (the ready event comes). Anything else is for
  // good: nothing starts it again.
  if (!client.ok() && client.status().code() != StatusCode::kUnavailable) {
    KLOG_WARN() << "LSP | " << editor->GetFilePath()
                << ": no language server: " << client.status().message();
  }
  return client.ok();
}

bool LspManager::NotifyOpened(IEditor *editor) {
  if (m_isShutdown || editor == nullptr) {
    return false;
  }
  const std::optional<LspServerKey> key = editor->GetServerKey();
  if (!key) {
    return false;
  }
  // Starts the server when there is none. When it is not ready yet,
  // wxEVT_LSP_SERVER_READY tells the editor to notify again.
  const auto client = GetClientFor(*key);
  if (!client.ok()) {
    KLOG_DEBUG() << "LSP | " << editor->GetFilePath() << ": "
                 << client.status().message();
    return false;
  }
  return (*client)->NotifyOpened(editor);
}

std::optional<RequestID> LspManager::GetSemanticHighlightsFor(IEditor *editor) {
  if (m_isShutdown || editor == nullptr) {
    return std::nullopt;
  }
  const std::optional<LspServerKey> key = editor->GetServerKey();
  if (!key) {
    return std::nullopt;
  }
  // Starts the server when there is none. When it is not ready yet,
  // wxEVT_LSP_SERVER_READY tells the editor to ask again.
  const auto client = GetClientFor(*key);
  if (!client.ok()) {
    KLOG_DEBUG() << "LSP | " << editor->GetFilePath() << ": "
                 << client.status().message();
    return std::nullopt;
  }
  return (*client)->GetSemanticHighlightsFor(editor);
}

void LspManager::NotifyClosed(IEditor *editor) {
  if (editor == nullptr) {
    return;
  }
  if (const auto client = FindClient(*editor)) {
    client->CloseDocument(editor);
  }
}

std::shared_ptr<LspClient> LspManager::FindClient(const IEditor &editor) const {
  const std::optional<LspServerKey> key = editor.GetServerKey();
  if (!key) {
    return nullptr;
  }
  const auto it = m_entries.find(key->GetKey());
  return it == m_entries.end() ? nullptr : it->second->m_client;
}

void LspManager::OnPageAdded(PageViewEvent &event) {
  event.Skip(); // The other views follow it too
  Page page;
  page.m_type = event.GetPageType();
  page.m_machine = event.GetAccount().GetId();
  page.m_workingDir = event.GetWorkingDir();
  page.m_path = event.GetPath();
  page.m_ref = event.GetRef();
  m_pages.push_back(std::move(page));
}

void LspManager::OnPageRemoved(PageViewEvent &event) {
  event.Skip();
  const bool isFile = event.GetPageType() == PageType::kFile;
  const wxString machine = event.GetAccount().GetId();
  const auto it =
      std::find_if(m_pages.begin(), m_pages.end(), [&](const Page &page) {
        if (isFile) {
          return page.m_type == PageType::kFile && page.m_machine == machine &&
                 page.m_path == event.GetPath();
        }
        return page.m_type != PageType::kFile && page.m_ref == event.GetRef();
      });
  if (it != m_pages.end()) {
    m_pages.erase(it);
  }
  if (isFile) {
    // Before the servers that nobody needs end: they are told first. The page
    // is still there, and gives its editor.
    IManager *manager = GetKennelManager();
    NotifyClosed(manager ? manager->FindEditorByUID(event.GetEditorUID())
                         : nullptr);
  }
  EndUnusedServers();
}

void LspManager::OnPageRenamed(PageViewEvent &event) {
  event.Skip();
  if (event.GetPageType() == PageType::kFile) {
    // A file that was saved under another name.
    const wxString machine = event.GetAccount().GetId();
    for (Page &page : m_pages) {
      if (page.m_type == PageType::kFile && page.m_machine == machine &&
          page.m_path == event.GetOldFilePath()) {
        page.m_path = event.GetPath();
        page.m_workingDir = event.GetWorkingDir();
      }
    }
    EndUnusedServers();
    return;
  }
  for (Page &page : m_pages) {
    if (page.m_type != PageType::kFile && page.m_ref == event.GetOldRef()) {
      page.m_ref = event.GetRef();
    }
  }
}

void LspManager::OnPageMoved(PageViewEvent &event) {
  event.Skip();
  // A terminal went to another folder.
  for (Page &page : m_pages) {
    if (page.m_type != PageType::kFile && page.m_ref == event.GetRef()) {
      page.m_workingDir = event.GetAccount().m_rootDir;
    }
  }
  EndUnusedServers();
}

void LspManager::OnPagesCleared(wxCommandEvent &event) {
  event.Skip();
  // Every page is gone: no server is needed, and no file is open.
  m_pages.clear();
  EndUnusedServers(/*force=*/true);
}

bool LspManager::IsNeeded(const LspServerKey &key) const {
  const wxString machine = key.m_account.GetId();
  return std::any_of(m_pages.begin(), m_pages.end(), [&](const Page &page) {
    if (page.m_machine != machine) {
      return false;
    }
    if (page.m_type == PageType::kFile) {
      return key.m_account.IsInRootDir(page.m_path);
    }
    return page.m_workingDir == key.m_account.m_rootDir;
  });
}

void LspManager::EndUnusedServers(bool force) {
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    // What the user declined stays declined.
    if (it->second->m_state == LspServerState::kDeclined ||
        (!force && IsNeeded(it->second->GetServerKey()))) {
      ++it;
      continue;
    }
    KLOG_DEBUG() << "LSP | " << it->first << ": "
                 << (force ? "ending it" : "no page needs it, ending it");
    it = m_entries.erase(it);
  }
}

void LspManager::Decline(const LspServerKey &key, bool remember) {
  auto it = m_entries.find(key.GetKey());
  if (it != m_entries.end() &&
      it->second->m_state == LspServerState::kAwaitingConsent) {
    KLOG_DEBUG() << "LSP | " << key.GetKey() << ": the user declined"
                 << (remember ? " (remembered)" : "");
    it->second->m_state = LspServerState::kDeclined;
    if (remember) {
      // Not asked again, on this machine, until the user takes the answer back.
      it->second->m_declineStored = true;
      const Status stored = AppManager::Get().GetDialogAnswers().Store(
          key.GetInstallAnswerKey(), key.GetInstallQuestion(), false);
      if (!stored.ok()) {
        KLOG_WARN() << "LSP | could not store the answer: " << stored.message();
      }
    }
  }
}
