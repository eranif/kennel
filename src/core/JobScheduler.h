#pragma once

#include <wx/datetime.h>
#include <wx/timer.h>

#include <functional>
#include <map>

#include "core/Job.h"

// Fires a callback for each configured job (AppManager::Get().Config().jobs).
// A job runs either on a fixed cadence measured in hours (ScheduleMode::
// kIntervalHours), anchored so ticks don't drift, or once a day at a fixed
// time (ScheduleMode::kDailyAt). The timer itself only polls once a minute.
class JobScheduler : public wxEvtHandler {
public:
  using RunFn = std::function<void(const JobDef &)>;

  explicit JobScheduler(RunFn onRun);
  ~JobScheduler() override;

  // Re-syncs schedules against the current jobs list: call once at startup
  // and again whenever the jobs list is added to, removed from, or edited
  // (e.g. after the Manage Jobs dialog saves). A job whose schedule changed
  // is re-anchored from now, so the new cadence takes effect right away
  // instead of only after the old one would have fired.
  void Reload();

private:
  // A job's pending run, plus the schedule fields it was computed from so
  // Reload() can tell an edited schedule from an untouched one.
  struct Entry {
    wxDateTime nextRun;
    ScheduleMode mode = ScheduleMode::kIntervalHours;
    int intervalHours = 1;
    int dailyHour = 10;
    int dailyMinute = 0;

    // True when `job` still has the schedule this entry was built from.
    bool Matches(const JobDef &job) const;
  };

  void OnTimer(wxTimerEvent &event);
  static wxDateTime ComputeNextRun(const JobDef &job, const wxDateTime &from);
  static Entry MakeEntry(const JobDef &job, const wxDateTime &from);

  RunFn m_onRun;
  wxTimer m_timer;
  std::map<wxString, Entry> m_schedules; // keyed by job name
};
