#include "app/TmuxStartup.hpp"

#include "core/Logger.h"

wxDEFINE_EVENT(wxEVT_TMUX_STARTUP_DONE, wxCommandEvent);

TmuxStartup::TmuxStartup(wxEvtHandler *handler) : m_handler(handler) {
  Bind(wxEVT_TMUX_PREPARED, &TmuxStartup::OnPrepared, this);
}

void TmuxStartup::Start(const std::vector<Request> &requests) {
  m_requests = requests;
  m_next = 0;
  m_machines.clear();
  StartNext();
}

const TmuxStartup::Machine *TmuxStartup::Find(const Account &account) const {
  for (const Machine &machine : m_machines) {
    if (machine.account.GetId() == account.GetId()) {
      return &machine;
    }
  }
  return nullptr;
}

void TmuxStartup::StartNext() {
  if (m_next >= m_requests.size()) {
    auto *event = new wxCommandEvent(wxEVT_TMUX_STARTUP_DONE);
    m_handler->QueueEvent(event);
    return;
  }
  const Request &request = m_requests[m_next++];
  m_preparer.Start(request.account, request.sessions);
}

void TmuxStartup::OnPrepared(TmuxPreparedEvent &event) {
  // The answer of the run that is current (the preparer drops the others).
  if (event.GetInt() != m_preparer.GetGeneration()) {
    return;
  }
  Machine machine;
  machine.account = m_requests[m_next - 1].account;
  switch (event.GetOutcome()) {
  case TmuxPreparedEvent::Outcome::kReady:
    machine.result = Result::kReady;
    machine.version = event.GetVersion();
    break;
  case TmuxPreparedEvent::Outcome::kWithoutTmux:
    machine.result = Result::kDeclined; // The user said no
    break;
  case TmuxPreparedEvent::Outcome::kCancelled:
    // An answer that is not a clear yes or no (the dialog was closed): nothing
    // is removed because of it.
    machine.result = Result::kUnavailable;
    machine.why = _("The question about tmux-shim was not answered.");
    break;
  case TmuxPreparedEvent::Outcome::kUnsupported:
  case TmuxPreparedEvent::Outcome::kFailed:
    machine.result = Result::kUnavailable;
    machine.why = event.GetString();
    KLOG_WARN() << "tmux on " << machine.account.GetDisplayName() << ": "
                << machine.why;
    break;
  }
  m_machines.push_back(std::move(machine));
  StartNext();
}
