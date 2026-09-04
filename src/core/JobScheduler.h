#pragma once

#include <wx/datetime.h>
#include <wx/timer.h>

#include <functional>
#include <map>

#include "core/Job.h"

// Fires a callback for each configured job (AppManager::Get().Config().jobs)
// on a fixed cadence measured in hours since the job became known to the
// scheduler (app start for jobs present at construction time, "now" for jobs
// picked up by a later Reload()). Anchored scheduling: after a job fires, its
// next run time is nextRun + intervalHours, so ticks don't drift even though
// the timer itself only polls once a minute.
class JobScheduler : public wxEvtHandler {
public:
  using RunFn = std::function<void(const JobDef &)>;

  explicit JobScheduler(RunFn onRun);
  ~JobScheduler() override;

  // Re-syncs schedules against the current jobs list: call once at startup
  // and again whenever the jobs list is added to, removed from, or edited
  // (e.g. after the Manage Jobs dialog saves).
  void Reload();

private:
  void OnTimer(wxTimerEvent &event);

  RunFn m_onRun;
  wxTimer m_timer;
  std::map<wxString, wxDateTime> m_nextRun; // keyed by job name
};
