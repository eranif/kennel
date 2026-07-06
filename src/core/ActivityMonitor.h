#pragma once

#include <wx/timer.h>

#include <functional>
#include <string>

// Tracks per-session activity by watching raw output chunks. It fires
// onActive() when the first chunk arrives after an idle period, and onIdle()
// when no chunk has been seen for idleMs milliseconds. Both transitions happen
// on the UI thread (the timer fires there; OnOutput() is called from
// wxTerminalViewCtrl's CallAfter path).
//
// Usage: call OnOutput() from the terminal's output callback. The monitor owns
// the wx timer, so it must be constructed on the UI thread.
class ActivityMonitor : public wxEvtHandler {
public:
  using NotifyFn = std::function<void()>;

  // idleMs: silence duration before onIdle fires (default 1 s).
  ActivityMonitor(NotifyFn onActive, NotifyFn onIdle, int idleMs = 3000);
  ~ActivityMonitor() override;

  // Call from the terminal output callback on every raw chunk.
  void OnOutput(const std::string &chunk);

  bool IsIdle() const { return m_idle; }

private:
  void OnTimer(wxTimerEvent &);

  NotifyFn m_onActive;
  NotifyFn m_onIdle;
  wxTimer m_timer;
  int m_idleMs = 3000;
  bool m_idle = true;
  bool m_hadOutput = false; // suppress the initial idle before first chunk
};
