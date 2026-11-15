#pragma once

#include "app/AsyncGuard.hpp"
#include "core/Account.h"
#include "core/RemoteInstaller.h"
#include "core/TmuxClient.h"
#include "core/TmuxCommands.h"

#include <memory>
#include <wx/event.h>

// The answer of TmuxPreparer.
class TmuxPreparedEvent : public wxCommandEvent {
public:
  enum class Outcome {
    kReady,       // tmux is there and new enough: the session can run in it
    kUnsupported, // tmux is missing or too old: GetString() says what to do
    kFailed,      // the machine could not be asked: GetString() says why
    kWithoutTmux, // the user chose to start the session without tmux
    kCancelled,   // the user cancelled: nothing is started
  };

  explicit TmuxPreparedEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  TmuxPreparedEvent(const TmuxPreparedEvent &) = default;
  wxEvent *Clone() const override { return new TmuxPreparedEvent(*this); }

  Outcome GetOutcome() const { return m_outcome; }
  void SetOutcome(Outcome outcome) { m_outcome = outcome; }
  // kReady: the version of the tmux of the machine.
  const tmux::Version &GetVersion() const { return m_version; }
  void SetVersion(const tmux::Version &version) { m_version = version; }

private:
  Outcome m_outcome{Outcome::kFailed};
  tmux::Version m_version;
};

// Sent once for each Start(), last. `GetInt()` is the GetGeneration() of that
// run. Nothing is sent when the preparer is
// destroyed or Start() is called again first.
wxDECLARE_EVENT(wxEVT_TMUX_PREPARED, TmuxPreparedEvent);

// What a session needs to know before it starts in tmux on a machine:
//  1. Is tmux there, and is it 3.3 or newer (`TmuxClient::Probe()`, on a worker
//     thread: ssh and wsl.exe block).
//  2. Is tmux-shim there (`RemoteInstaller`, check only). When it is not, and
//     there is a build of it for the machine, the user is asked whether to
//     download it (Download, Continue without tmux, or Cancel), and it is
//     installed when they say Download. A shim that can not be had (no build,
//     the host can not be reached, the install failed) never stops the session:
//     it runs in tmux without it (no activity icon).
// The answer is a wxEVT_TMUX_PREPARED event, to the handler given here, which
// must outlive this object. Only the UI thread may call the methods.
class TmuxPreparer : public wxEvtHandler {
public:
  explicit TmuxPreparer(wxEvtHandler *handler);
  ~TmuxPreparer() override;

  // Starts (a run that is going on is dropped). `restoring` > 0: it is the
  // number of saved sessions that wait for this machine (TmuxStartup): the
  // question about the shim then says so, and its choices are Download or
  // removing those sessions (the answer "No" is kWithoutTmux; there is no
  // Cancel).
  void Start(const Account &account, int restoring = 0);
  // Drops the run that is going on: no event comes for it, also one that was
  // queued already (it has an older GetGeneration() than the current one).
  void Cancel();
  // The number of the run that is current: the `GetInt()` of the event that
  // answers it. An event with another number is from a run that was dropped.
  int GetGeneration() const { return m_generation; }

private:
  // What the preparer waits for.
  enum class Phase { kIdle, kProbing, kCheckingShim, kInstallingShim };

  void OnProbed(const StatusOr<TmuxProbe> &probe);
  void OnInstallDone(InstallEvent &event);
  // Asks the user whether to download tmux-shim for the machine: wxID_YES
  // (download), wxID_NO (start without tmux) or wxID_CANCEL.
  int AskToInstallShim() const;
  void Finish(TmuxPreparedEvent::Outcome outcome, const wxString &text = {});

  wxEvtHandler *m_handler;
  AliveFlag m_alive{MakeAliveFlag()};
  Account m_account;
  int m_restoring{0}; // See Start()
  tmux::Version m_version;
  Phase m_phase{Phase::kIdle};
  // Tells a run from the one before it: an answer of a thread that was started
  // by an earlier Start() is dropped.
  int m_generation{0};
  std::unique_ptr<RemoteInstaller> m_installer;
};
