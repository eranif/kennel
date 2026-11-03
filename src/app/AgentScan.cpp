#include "app/AgentScan.hpp"

#include "core/EventNotifier.hpp"

#include <atomic>
#include <thread>

wxDEFINE_EVENT(wxEVT_AGENT_SCAN, AgentScanEvent);

namespace {
std::atomic<bool> g_stopped{false};

// Sends the event through the EventNotifier, unless the application is going
// away.
void Post(const AgentScanEvent &event) {
  if (!g_stopped.load()) {
    EventNotifier::Get()->QueueEvent(event.Clone());
  }
}
} // namespace

namespace AgentScan {

// The threads are detached and are not waited for: a scan can sit in an ssh or
// wsl.exe call for up to 30 seconds, and the application must not wait for it
// to close. A thread that is still running when the process ends is stopped
// with it; after Stop() it sends nothing. (The EventNotifier lives until the
// process ends, so the one call that can still be in progress is harmless.)
void Stop() { g_stopped.store(true); }

void StartLocal() {
  // Detached: it only needs the target, which it asks for each time.
  std::thread([] {
    AgentScanEvent event(wxEVT_AGENT_SCAN);
    event.local = true;
    event.last = true;
    event.agents = agentscan::ScanLocal();
    Post(event);
  }).detach();
}

void StartRemote(std::vector<agentscan::WslTarget> distros,
                 std::vector<SshHostEntry> hosts) {
  std::thread([distros = std::move(distros), hosts = std::move(hosts)] {
    const size_t total = distros.size() + hosts.size();
    size_t index = 0;
    // Tells the user where the search is now: a host can take a while.
    const auto starting = [&](const wxString &where) {
      AgentScanEvent event(wxEVT_AGENT_SCAN);
      event.starting = true;
      event.where = where;
      event.index = ++index;
      event.total = total;
      Post(event);
    };
    for (const auto &distro : distros) {
      starting("WSL: " + distro.distro);
      AgentScanEvent event(wxEVT_AGENT_SCAN);
      event.where = "WSL: " + distro.distro;
      event.agents = agentscan::ScanWsl(distro);
      Post(event);
    }
    for (const auto &host : hosts) {
      const wxString where = host.name.empty() ? host.address : host.name;
      starting(where);
      AgentScanEvent event(wxEVT_AGENT_SCAN);
      event.where = where;
      event.agents = agentscan::ScanRemote(host);
      Post(event);
    }
    AgentScanEvent done(wxEVT_AGENT_SCAN);
    done.last = true;
    Post(done);
  }).detach();
}

} // namespace AgentScan
