#include "app/TmuxPreparer.hpp"

#include "core/Logger.h"
#include "core/TmuxClient.h"
#include "core/TmuxShim.h"

#include <thread>
#include <wx/app.h>
#include <wx/msgdlg.h>

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

void TmuxPreparer::Cancel() {
  m_installer.reset();
  m_phase = Phase::kIdle;
  ++m_generation;
}

void TmuxPreparer::OnProbed(const StatusOr<TmuxProbe> &probe) {
  const wxString where = m_account.GetDisplayName();
  if (!probe.ok()) {
    Finish(TmuxPreparedEvent::Outcome::kFailed, probe.status().message());
    return;
  }
  if (!probe->installed) {
    Finish(TmuxPreparedEvent::Outcome::kUnsupported,
           wxString::Format(
               _("tmux is not installed on %s. Install tmux %d.%d or newer "
                 "(for example with apt or brew)."),
               where, tmux::kMinVersion.major, tmux::kMinVersion.minor));
    return;
  }
  if (!probe->version) {
    Finish(
        TmuxPreparedEvent::Outcome::kUnsupported,
        wxString::Format(
            _("Could not tell which version of tmux %s has (it says \"%s\")."),
            where, probe->banner));
    return;
  }
  if (!probe->IsSupported()) {
    Finish(
        TmuxPreparedEvent::Outcome::kUnsupported,
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
  case Phase::kCheckingShim: {
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
    // The dialog runs events: the page that owns this object may be closed
    // (it then destroys it), or Start() called again (this answer is stale).
    // Nothing of this object may be used after that.
    const AliveFlag alive = m_alive;
    const int generation = m_generation;
    const int answer = AskToInstallShim();
    if (!alive->load() || generation != m_generation) {
      return;
    }
    switch (answer) {
    case wxID_YES:
      break;
    case wxID_NO:
      Finish(TmuxPreparedEvent::Outcome::kWithoutTmux);
      return;
    default:
      Finish(TmuxPreparedEvent::Outcome::kCancelled);
      return;
    }
    m_phase = Phase::kInstallingShim;
    if (const Status started = m_installer->Start(
            m_account, tmuxshim::Tool(), tmuxshim::InstallDir(),
            RemoteInstaller::Mode::kInstall);
        !started.ok()) {
      KLOG_WARN() << "tmux-shim: could not install on " << where << ": "
                  << started.message();
      Finish(TmuxPreparedEvent::Outcome::kReady);
    }
    return;
  }
  case Phase::kInstallingShim:
    if (!event.GetResult().ok()) {
      KLOG_WARN() << "tmux-shim: install on " << where
                  << " failed: " << event.GetResult().message();
    }
    Finish(TmuxPreparedEvent::Outcome::kReady);
    return;
  case Phase::kIdle:
  case Phase::kProbing:
    return; // Not waiting for the installer
  }
}

int TmuxPreparer::AskToInstallShim() const {
  const ToolDescription &tool = tmuxshim::Tool();
  wxMessageDialog dialog(
      wxTheApp->GetTopWindow(),
      // The main instruction: short.
      wxString::Format(_("Download %s?"), tool.name),
      wxString::Format(_("Kennel requires to download %s"), tool.name),
      wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxICON_QUESTION);
  dialog.SetExtendedMessage(wxString::Format(
      _("%s %s is downloaded from GitHub into ~/.kennel/bin on %s. It lets "
        "Kennel show what the agent is doing (working, done, waiting) for a "
        "session that runs in tmux."),
      tool.name, tool.version, m_account.GetDisplayName()));
  dialog.SetYesNoCancelLabels(_("Download"), _("Continue without tmux"),
                              _("Cancel"));
  return dialog.ShowModal();
}

void TmuxPreparer::Finish(TmuxPreparedEvent::Outcome outcome,
                          const wxString &text) {
  m_phase = Phase::kIdle;
  auto *event = new TmuxPreparedEvent(wxEVT_TMUX_PREPARED);
  event->SetOutcome(outcome);
  event->SetVersion(m_version);
  event->SetString(text);
  event->SetInt(m_generation);
  m_handler->QueueEvent(event);
}
