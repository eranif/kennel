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
    kReady,     // tmux is there and new enough: sessions can run in it
    kNeedsShim, // tmux is fine, tmux-shim is not on the machine: InstallShim()
    kUnavailable, // tmux is missing or too old, or the machine can not be
                  // reached: GetString() says what is wrong
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
  Outcome m_outcome{Outcome::kUnavailable};
  tmux::Version m_version;
};

// Sent for each answer, last. The event object is the TmuxPreparer, and
// `GetInt()` is its GetGeneration() for that run. Nothing is sent when the
// preparer is destroyed or Start() is called again first (the answer of the
// earlier run is dropped).
wxDECLARE_EVENT(wxEVT_TMUX_PREPARED, TmuxPreparedEvent);

// What a machine needs before sessions can start in tmux on it:
//  1. Is tmux there, and is it 3.3 or newer (`TmuxClient::Probe()`, on a worker
//     thread: ssh and wsl.exe block). No: kUnavailable.
//  2. Is tmux-shim there (`RemoteInstaller`, check only; it changes nothing on
//     the machine). It is there, or there is no build of it for the machine, or
//     it can not be checked: kReady (a session then runs in tmux without it: no
//     activity icon). It is not there and there is a build: kNeedsShim. The
//     preparer asks nothing and shows nothing: the owner asks the user, and
//     calls InstallShim() when the user agrees.
//  3. InstallShim(): downloads it and installs it on the machine; kReady when
//     that is done (also when it failed: the sessions then run without it).
// The answers are wxEVT_TMUX_PREPARED events, to the handler given here, which
// must outlive this object. Only the UI thread may call the methods.
class TmuxPreparer : public wxEvtHandler {
public:
  explicit TmuxPreparer(wxEvtHandler *handler);
  ~TmuxPreparer() override;

  // Starts (a run that is going on is dropped).
  void Start(const Account &account);
  // After kNeedsShim: the user agreed to the download. Nothing otherwise.
  void InstallShim();
  // The number of the run that is current: the `GetInt()` of the event that
  // answers it. An event with another number is from a run that was dropped.
  int GetGeneration() const { return m_generation; }

private:
  // What the preparer waits for.
  enum class Phase {
    kIdle,
    kProbing,
    kCheckingShim,
    kWaitingForUser,
    kInstallingShim
  };

  void OnProbed(const StatusOr<TmuxProbe> &probe);
  void OnInstallDone(InstallEvent &event);
  void Finish(TmuxPreparedEvent::Outcome outcome, const wxString &text = {});

  wxEvtHandler *m_handler;
  AliveFlag m_alive{MakeAliveFlag()};
  Account m_account;
  tmux::Version m_version;
  Phase m_phase{Phase::kIdle};
  // Tells a run from the one before it: an answer of a thread that was started
  // by an earlier Start() is dropped.
  int m_generation{0};
  std::unique_ptr<RemoteInstaller> m_installer;
};
