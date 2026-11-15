#include "app/TmuxMachines.hpp"

#include "core/EventNotifier.hpp"

wxDEFINE_EVENT(wxEVT_TMUX_MACHINE_PREPARE, TmuxMachineEvent);
wxDEFINE_EVENT(wxEVT_TMUX_MACHINE_INSTALL_SHIM, TmuxMachineEvent);
wxDEFINE_EVENT(wxEVT_TMUX_MACHINE, TmuxMachineEvent);

TmuxMachines::TmuxMachines() {
  EventNotifier::Get()->Bind(wxEVT_TMUX_MACHINE_PREPARE,
                             &TmuxMachines::OnPrepareRequested, this);
  EventNotifier::Get()->Bind(wxEVT_TMUX_MACHINE_INSTALL_SHIM,
                             &TmuxMachines::OnInstallShimRequested, this);
  Bind(wxEVT_TMUX_PREPARED, &TmuxMachines::OnPrepared, this);
}

TmuxMachines::~TmuxMachines() {
  EventNotifier::Get()->Unbind(wxEVT_TMUX_MACHINE_PREPARE,
                               &TmuxMachines::OnPrepareRequested, this);
  EventNotifier::Get()->Unbind(wxEVT_TMUX_MACHINE_INSTALL_SHIM,
                               &TmuxMachines::OnInstallShimRequested, this);
}

void TmuxMachines::Prepare(const Account &account, bool force) {
  std::unique_ptr<Machine> &slot = m_machines[account.GetId()];
  if (!slot) {
    slot = std::make_unique<Machine>();
    slot->account = account;
    slot->preparer = std::make_unique<TmuxPreparer>(this);
  }
  Machine &machine = *slot;
  switch (machine.state) {
  case TmuxMachineEvent::State::kReady:
  case TmuxMachineEvent::State::kNeedsShim:
    if (!force) {
      Publish(machine); // Known: the page that asks learns it
      return;
    }
    break;
  case TmuxMachineEvent::State::kInstalling:
    Publish(machine); // The page that asks shows the download too
    return;
  case TmuxMachineEvent::State::kProbing:
    return; // An answer is on its way, and it goes to every page
  case TmuxMachineEvent::State::kUnavailable:
    break; // Not asked yet, or not usable the last time: ask again
  }
  machine.state = TmuxMachineEvent::State::kProbing;
  machine.why.clear();
  machine.preparer->Start(account);
  Publish(machine);
}

void TmuxMachines::OnPrepareRequested(TmuxMachineEvent &event) {
  Prepare(event.GetAccount(), event.IsForced());
}

void TmuxMachines::OnInstallShimRequested(TmuxMachineEvent &event) {
  const auto it = m_machines.find(event.GetAccount().GetId());
  if (it == m_machines.end() ||
      it->second->state != TmuxMachineEvent::State::kNeedsShim) {
    return; // Not waiting for it (another page asked already)
  }
  Machine &machine = *it->second;
  machine.state = TmuxMachineEvent::State::kInstalling;
  machine.preparer->InstallShim();
  Publish(machine);
}

void TmuxMachines::OnPrepared(TmuxPreparedEvent &event) {
  for (const auto &[id, machine] : m_machines) {
    if (machine->preparer.get() != event.GetEventObject()) {
      continue;
    }
    if (event.GetInt() != machine->preparer->GetGeneration()) {
      return; // The answer of a run that was dropped
    }
    switch (event.GetOutcome()) {
    case TmuxPreparedEvent::Outcome::kReady:
      machine->state = TmuxMachineEvent::State::kReady;
      machine->version = event.GetVersion();
      break;
    case TmuxPreparedEvent::Outcome::kNeedsShim:
      machine->state = TmuxMachineEvent::State::kNeedsShim;
      machine->version = event.GetVersion();
      break;
    case TmuxPreparedEvent::Outcome::kUnavailable:
      machine->state = TmuxMachineEvent::State::kUnavailable;
      machine->why = event.GetString();
      break;
    }
    Publish(*machine);
    return;
  }
}

void TmuxMachines::Publish(const Machine &machine) const {
  auto *event = new TmuxMachineEvent(wxEVT_TMUX_MACHINE);
  event->SetAccount(machine.account);
  event->SetState(machine.state);
  event->SetVersion(machine.version);
  event->SetString(machine.why);
  EventNotifier::Get()->QueueEvent(event);
}
