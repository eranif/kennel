#pragma once

#include "core/AgentScanner.h"

#include <vector>
#include <wx/event.h>

// What a scan for agents found. Sent through the
// EventNotifier, on the UI thread.
class AgentScanEvent : public wxEvent {
public:
  explicit AgentScanEvent(wxEventType type = wxEVT_NULL) : wxEvent(0, type) {}
  wxEvent *Clone() const override { return new AgentScanEvent(*this); }

  std::vector<AgentDef> agents;
  // What was searched: "this computer", "WSL: Ubuntu", a host.
  wxString where;
  bool local{false}; // The scan of this machine
  bool last{false};  // Nothing more comes from this scan
  // Not a result: the search on `where` is starting (`index` of `total`).
  bool starting{false};
  size_t index{0};
  size_t total{0};
};

wxDECLARE_EVENT(wxEVT_AGENT_SCAN, AgentScanEvent);

// Looks for agents in the background (a thread each): the user interface never
// waits. The results come as wxEVT_AGENT_SCAN events.
namespace AgentScan {

// The scans send their events through the EventNotifier. After Stop() a scan
// that is still running ends without sending anything: call it when the
// application closes.
void Stop();

// This machine. One event, with `last` set.
void StartLocal();

// The WSL distros and the SSH hosts, one after the other: an event for each,
// then one at the end with `last` set.
void StartRemote(std::vector<agentscan::WslTarget> distros,
                 std::vector<SshHostEntry> hosts);

} // namespace AgentScan
