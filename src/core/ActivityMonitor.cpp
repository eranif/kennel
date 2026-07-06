#include "core/ActivityMonitor.h"

ActivityMonitor::ActivityMonitor(NotifyFn onActive, NotifyFn onIdle, int idleMs)
    : m_onActive(std::move(onActive)), m_onIdle(std::move(onIdle)),
      m_timer(this), m_idleMs(idleMs) {
  m_timer.SetOwner(this);
  Bind(wxEVT_TIMER, &ActivityMonitor::OnTimer, this);
}

ActivityMonitor::~ActivityMonitor() {
  m_timer.Stop();
  Unbind(wxEVT_TIMER, &ActivityMonitor::OnTimer, this);
}

void ActivityMonitor::OnOutput(const std::string &chunk) {
  if (chunk.empty()) {
    return;
  }
  m_hadOutput = true;
  m_timer.StartOnce(m_idleMs);
  if (m_idle) {
    m_idle = false;
    if (m_onActive) {
      m_onActive();
    }
  }
}

void ActivityMonitor::OnTimer(wxTimerEvent &) {
  if (!m_hadOutput) {
    return; // never saw output — stay quiet, don't fire idle
  }
  if (!m_idle) {
    m_idle = true;
    if (m_onIdle) {
      m_onIdle();
    }
  }
}
