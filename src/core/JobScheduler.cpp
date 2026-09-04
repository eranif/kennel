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

void JobScheduler::Reload() {
  const wxDateTime now = wxDateTime::Now();
  const auto &jobs = AppManager::Get().Config().jobs;

  // Drop schedules for jobs that were deleted or renamed.
  for (auto it = m_nextRun.begin(); it != m_nextRun.end();) {
    bool stillExists = false;
    for (const JobDef &j : jobs) {
      if (j.name == it->first) {
        stillExists = true;
        break;
      }
    }
    it = stillExists ? std::next(it) : m_nextRun.erase(it);
  }

  // Anchor newly-seen jobs from now.
  for (const JobDef &j : jobs) {
    if (!m_nextRun.contains(j.name)) {
      m_nextRun[j.name] = now + wxTimeSpan::Hours(j.intervalHours);
    }
  }
}

void JobScheduler::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  const wxDateTime now = wxDateTime::Now();
  const auto &jobs = AppManager::Get().Config().jobs;

  for (const JobDef &j : jobs) {
    auto it = m_nextRun.find(j.name);
    if (it == m_nextRun.end()) {
      m_nextRun[j.name] = now + wxTimeSpan::Hours(j.intervalHours);
      continue;
    }
    if (now < it->second) {
      continue;
    }

    if (!j.enabled) {
      // Disabled: don't run, but resync the schedule so re-enabling it later
      // doesn't immediately fire a backlog of catch-up runs.
      it->second = now + wxTimeSpan::Hours(j.intervalHours);
      continue;
    }

    KLOG_INFO() << "Job '" << j.name << "' is due; running";
    m_onRun(j);

    it->second += wxTimeSpan::Hours(j.intervalHours);
    if (now >= it->second) {
      // We were behind by more than one interval (e.g. the machine slept);
      // resync from now instead of firing a burst of catch-up runs.
      it->second = now + wxTimeSpan::Hours(j.intervalHours);
    }
  }
}
