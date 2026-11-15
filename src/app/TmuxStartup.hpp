#pragma once

#include "app/TmuxPreparer.hpp"
#include "core/Account.h"
#include "core/TmuxCommands.h"

#include <vector>
#include <wx/event.h>

// Sent to the handler given to TmuxStartup when every machine has an answer.
wxDECLARE_EVENT(wxEVT_TMUX_STARTUP_DONE, wxCommandEvent);

// What is done before Kennel opens the saved sessions: the machines that hold
// sessions in tmux are prepared (TmuxPreparer: is tmux there, is tmux-shim
// there, may it be downloaded), one after the other, before the first terminal
// is started. A terminal then never has to wait for a machine, and a machine
// that is asked a question is asked once, not for each session.
//
// The answer for each machine (Find()):
//  - kReady: the sessions of it open in tmux (the version of its tmux is
//  given);
//  - kDeclined: the user does not want tmux-shim: the sessions are removed;
//  - kUnavailable: the machine can not be used now (not reachable, no tmux, an
//    old one): its sessions are not opened this time, and stay saved.
// Only the UI thread may use it.
class TmuxStartup : public wxEvtHandler {
public:
  enum class Result { kReady, kDeclined, kUnavailable };

  struct Request {
    Account account;
    int sessions{0}; // How many saved sessions wait for the machine
  };

  struct Machine {
    Account account;
    Result result{Result::kUnavailable};
    tmux::Version version; // kReady
    wxString why;          // kUnavailable
  };

  // `handler` gets wxEVT_TMUX_STARTUP_DONE; it must outlive this object.
  explicit TmuxStartup(wxEvtHandler *handler);

  // Prepares the machines in the order given (each one is once in the list).
  // Not for a second run.
  void Start(const std::vector<Request> &requests);

  // The answer for `account` (the start folder does not count), or nothing for
  // a machine that was not asked.
  const Machine *Find(const Account &account) const;

private:
  void StartNext();
  void OnPrepared(TmuxPreparedEvent &event);

  wxEvtHandler *m_handler;
  TmuxPreparer m_preparer{this};
  std::vector<Request> m_requests;
  size_t m_next{0}; // The request that the preparer works on is m_next - 1
  std::vector<Machine> m_machines;
};
