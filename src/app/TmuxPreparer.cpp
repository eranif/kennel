#include "app/TmuxPreparer.hpp"

#include "core/Logger.h"
#include "core/TmuxClient.h"
#include "core/TmuxShim.h"

#include <thread>

wxDEFINE_EVENT(wxEVT_TMUX_PREPARED, TmuxPreparedEvent);

TmuxPreparer::TmuxPreparer(wxEvtHandler *handler) : m_handler(handler) {
  Bind(wxEVT_INSTALL_DONE, &TmuxPreparer::OnInstallDone, this);
}

TmuxPreparer::~TmuxPreparer() { m_alive->store(false); }

void TmuxPreparer::Start(const Account &account) {
  m_installer.reset(); // A run that is going on gets no more events
  m_account = account;
  m_phase = Phase::kProbing;
  const int generation = ++m_generation;
  // Blocks (ssh, wsl.exe): not on the UI thread. The thread uses no member.
  std::thread([this, alive = m_alive, account, generation] {
    const StatusOr<TmuxProbe> probe = TmuxClient::Probe(account);
    CallAfterIfAlive(alive, [this, probe, generation] {
      if (generation == m_generation) {
        OnProbed(probe);
      }
    });
  }).detach();
}

void TmuxPreparer::InstallShim() {
  if (m_phase != Phase::kWaitingForUser || !m_installer) {
    return;
  }
  m_phase = Phase::kInstallingShim;
  if (const Status started = m_installer->Start(
          m_account, tmuxshim::Tool(), tmuxshim::InstallDir(),
          RemoteInstaller::Mode::kInstall);
      !started.ok()) {
    KLOG_WARN() << "tmux-shim: could not install on "
                << m_account.GetDisplayName() << ": " << started.message();
    Finish(TmuxPreparedEvent::Outcome::kReady);
  }
}

void TmuxPreparer::OnProbed(const StatusOr<TmuxProbe> &probe) {
  const wxString where = m_account.GetDisplayName();
  if (!probe.ok()) {
    Finish(TmuxPreparedEvent::Outcome::kUnavailable,
           wxString::Format(_("Could not reach %s: %s"), where,
                            probe.status().message()));
    return;
  }
  if (!probe->installed) {
    Finish(TmuxPreparedEvent::Outcome::kUnavailable,
           wxString::Format(
               _("tmux is not installed on %s. Install tmux %d.%d or newer "
                 "(for example with apt or brew)."),
               where, tmux::kMinVersion.major, tmux::kMinVersion.minor));
    return;
  }
  if (!probe->version) {
    Finish(
        TmuxPreparedEvent::Outcome::kUnavailable,
        wxString::Format(
            _("Could not tell which version of tmux %s has (it says \"%s\")."),
            where, probe->banner));
    return;
  }
  if (!probe->IsSupported()) {
    Finish(
        TmuxPreparedEvent::Outcome::kUnavailable,
        wxString::Format(_("%s on %s is older than %d.%d, which Kennel needs."),
                         probe->banner, where, tmux::kMinVersion.major,
                         tmux::kMinVersion.minor));
    return;
  }
  m_version = *probe->version;

  // Is tmux-shim there? Changes nothing on the machine.
  m_installer = std::make_unique<RemoteInstaller>(this);
  m_phase = Phase::kCheckingShim;
  const Status started =
      m_installer->Start(m_account, tmuxshim::Tool(), tmuxshim::InstallDir(),
                         RemoteInstaller::Mode::kCheckOnly);
  if (!started.ok()) {
    KLOG_WARN() << "tmux-shim: could not check " << where << ": "
                << started.message();
    Finish(TmuxPreparedEvent::Outcome::kReady);
  }
}

void TmuxPreparer::OnInstallDone(InstallEvent &event) {
  const wxString where = m_account.GetDisplayName();
  switch (m_phase) {
  case Phase::kCheckingShim:
    if (!event.GetResult().ok()) {
      // No build for the machine, or it can not be reached: run without.
      KLOG_INFO() << "tmux-shim: not used on " << where << ": "
                  << event.GetResult().message();
      Finish(TmuxPreparedEvent::Outcome::kReady);
      return;
    }
    if (!event.NeedsInstall()) {
      Finish(TmuxPreparedEvent::Outcome::kReady);
      return;
    }
    // The owner asks the user, and calls InstallShim() when they agree.
    Finish(TmuxPreparedEvent::Outcome::kNeedsShim);
    m_phase = Phase::kWaitingForUser;
    return;
  case Phase::kInstallingShim:
    if (!event.GetResult().ok()) {
      KLOG_WARN() << "tmux-shim: install on " << where
                  << " failed: " << event.GetResult().message();
    }
    Finish(TmuxPreparedEvent::Outcome::kReady);
    return;
  case Phase::kIdle:
  case Phase::kProbing:
  case Phase::kWaitingForUser:
    return; // Not waiting for the installer
  }
}

void TmuxPreparer::Finish(TmuxPreparedEvent::Outcome outcome,
                          const wxString &text) {
  m_phase = Phase::kIdle;
  auto *event = new TmuxPreparedEvent(wxEVT_TMUX_PREPARED);
  event->SetEventObject(this);
  event->SetOutcome(outcome);
  event->SetVersion(m_version);
  event->SetString(text);
  event->SetInt(m_generation);
  m_handler->QueueEvent(event);
}
