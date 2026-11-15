#pragma once

#include "app/TmuxPreparer.hpp"
#include "core/Account.h"
#include "core/TmuxCommands.h"

#include <map>
#include <memory>
#include <wx/event.h>

// The events between the pages that run in tmux and TmuxMachines, through
// EventNotifier. A page never asks a machine itself: it says which machine it
// needs, and learns what is known about it.
class TmuxMachineEvent : public wxCommandEvent {
public:
  // What is known about a machine.
  enum class State {
    kProbing,     // being asked (tmux, tmux-shim)
    kNeedsShim,   // tmux is fine, tmux-shim is not there: it may be downloaded
    kInstalling,  // tmux-shim is being downloaded
    kReady,       // sessions can start in tmux: GetVersion()
    kUnavailable, // no tmux, too old, or not reachable: GetString() says why
  };

  explicit TmuxMachineEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  TmuxMachineEvent(const TmuxMachineEvent &) = default;
  wxEvent *Clone() const override { return new TmuxMachineEvent(*this); }

  const Account &GetAccount() const { return m_account; }
  void SetAccount(const Account &account) { m_account = account; }
  State GetState() const { return m_state; }
  void SetState(State state) { m_state = state; }
  const tmux::Version &GetVersion() const { return m_version; }
  void SetVersion(const tmux::Version &version) { m_version = version; }
  // wxEVT_TMUX_MACHINE_PREPARE: ask the machine again, also when it is known.
  bool IsForced() const { return m_forced; }
  void SetForced(bool forced) { m_forced = forced; }

private:
  Account m_account;
  State m_state{State::kProbing};
  tmux::Version m_version;
  bool m_forced{false};
};

// A page asks for a machine (GetAccount()). It gets wxEVT_TMUX_MACHINE for it,
// now or when the machine has answered; the account tells the pages apart.
wxDECLARE_EVENT(wxEVT_TMUX_MACHINE_PREPARE, TmuxMachineEvent);
// The user agreed to download tmux-shim on the machine (GetAccount()).
wxDECLARE_EVENT(wxEVT_TMUX_MACHINE_INSTALL_SHIM, TmuxMachineEvent);
// What is known about a machine: sent when it changes, and when a page asks
// for a machine that is known already. The state is in GetState().
wxDECLARE_EVENT(wxEVT_TMUX_MACHINE, TmuxMachineEvent);

// Knows the machines that sessions run in tmux on, and prepares each one once
// (TmuxPreparer, on worker threads, all machines at the same time), however
// many sessions ask. It asks nothing of the user and shows nothing: it listens
// to wxEVT_TMUX_MACHINE_PREPARE and wxEVT_TMUX_MACHINE_INSTALL_SHIM, and
// answers with wxEVT_TMUX_MACHINE. A machine that is ready stays ready; one
// that is not usable is asked again at the next request. Only the UI thread may
// use it.
class TmuxMachines : public wxEvtHandler {
public:
  TmuxMachines();
  ~TmuxMachines() override;

  // Starts to prepare the machine, so that the sessions that need it find it
  // ready (the same as a wxEVT_TMUX_MACHINE_PREPARE event).
  void Prepare(const Account &account, bool force = false);

private:
  struct Machine {
    Account account;
    std::unique_ptr<TmuxPreparer> preparer;
    // A new machine is kUnavailable until it is asked.
    TmuxMachineEvent::State state{TmuxMachineEvent::State::kUnavailable};
    tmux::Version version;
    wxString why;
  };

  void OnPrepareRequested(TmuxMachineEvent &event);
  void OnInstallShimRequested(TmuxMachineEvent &event);
  void OnPrepared(TmuxPreparedEvent &event);
  // Tells the pages (EventNotifier).
  void Publish(const Machine &machine) const;

  // By Account::GetId().
  std::map<wxString, std::unique_ptr<Machine>> m_machines;
};
