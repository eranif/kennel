#include "core/JobScheduler.h"

#include "core/AppManager.h"
#include "core/Logger.h"

namespace {
constexpr int kTickMs = 60 * 1000; // Poll once a minute.
}

JobScheduler::JobScheduler(RunFn onRun) : m_onRun(std::move(onRun)) {
  m_timer.SetOwner(this);
  m_timer.Start(kTickMs);
  Bind(wxEVT_TIMER, &JobScheduler::OnTimer, this, m_timer.GetId());
}

JobScheduler::~JobScheduler() {
  m_timer.Stop();
  Unbind(wxEVT_TIMER, &JobScheduler::OnTimer, this, m_timer.GetId());
}

bool JobScheduler::Entry::Matches(const JobDef &job) const {
  if (mode != job.scheduleMode) {
    return false;
  }
  if (mode == ScheduleMode::kDailyAt) {
    return dailyHour == job.dailyHour && dailyMinute == job.dailyMinute;
  }
  return intervalHours == job.intervalHours;
}

wxDateTime JobScheduler::ComputeNextRun(const JobDef &job,
                                        const wxDateTime &from) {
  if (job.scheduleMode == ScheduleMode::kDailyAt) {
    wxDateTime next = from;
    next.SetHour(job.dailyHour);
    next.SetMinute(job.dailyMinute);
    next.SetSecond(0);
    next.SetMillisecond(0);
    if (next <= from) {
      next += wxDateSpan::Days(1);
    }
    return next;
  }
  return from + wxTimeSpan::Hours(job.intervalHours);
}

JobScheduler::Entry JobScheduler::MakeEntry(const JobDef &job,
                                            const wxDateTime &from) {
  Entry entry;
  entry.mode = job.scheduleMode;
  entry.intervalHours = job.intervalHours;
  entry.dailyHour = job.dailyHour;
  entry.dailyMinute = job.dailyMinute;
  entry.nextRun = ComputeNextRun(job, from);
  return entry;
}

void JobScheduler::Reload() {
  const wxDateTime now = wxDateTime::Now();
  const auto &jobs = AppManager::Get().Config().jobs;

  // Drop schedules for jobs that were deleted or renamed.
  for (auto it = m_schedules.begin(); it != m_schedules.end();) {
    bool stillExists = false;
    for (const JobDef &j : jobs) {
      if (j.name == it->first) {
        stillExists = true;
        break;
      }
    }
    it = stillExists ? std::next(it) : m_schedules.erase(it);
  }

  for (const JobDef &j : jobs) {
    auto it = m_schedules.find(j.name);
    if (it == m_schedules.end()) {
      // Newly-seen job: anchor from now.
      m_schedules[j.name] = MakeEntry(j, now);
      continue;
    }
    if (!it->second.Matches(j)) {
      it->second = MakeEntry(j, now);
      KLOG_INFO() << "Job '" << j.name << "' schedule changed; next run at "
                  << it->second.nextRun.FormatISOCombined(' ');
    }
  }
}

void JobScheduler::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  const wxDateTime now = wxDateTime::Now();
  const auto &jobs = AppManager::Get().Config().jobs;

  for (const JobDef &j : jobs) {
    auto it = m_schedules.find(j.name);
    if (it == m_schedules.end()) {
      m_schedules[j.name] = MakeEntry(j, now);
      continue;
    }
    if (now < it->second.nextRun) {
      continue;
    }

    if (!j.enabled) {
      // Disabled: don't run, but resync the schedule so re-enabling it later
      // doesn't immediately fire a backlog of catch-up runs.
      it->second.nextRun = ComputeNextRun(j, now);
      continue;
    }

    KLOG_INFO() << "Job '" << j.name << "' is due; running";
    m_onRun(j);

    it->second.nextRun = ComputeNextRun(j, it->second.nextRun);
    if (now >= it->second.nextRun) {
      // We were behind by more than one interval (e.g. the machine slept);
      // resync from now instead of firing a burst of catch-up runs.
      it->second.nextRun = ComputeNextRun(j, now);
    }
  }
}
