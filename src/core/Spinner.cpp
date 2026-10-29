#include "core/Spinner.h"
#include "core/AppManager.h"

Spinner::Spinner(SpinnerDrawCB cb) : m_callback{std::move(cb)}, m_timer(this) {
  Bind(wxEVT_TIMER, &Spinner::OnTimer, this, m_timer.GetId());
}

Spinner::~Spinner() {
  m_callback = nullptr;
  m_timer.Stop();
  Unbind(wxEVT_TIMER, &Spinner::OnTimer, this, m_timer.GetId());
}

void Spinner::Start() {
  if (m_timer.IsRunning()) {
    return;
  }
  if (!s_frames[0].IsOk()) {
    const auto &bitmaps = AppManager::Get().GetBitmaps();
    for (int i = 0; i < kFrameCount; ++i) {
      s_frames[i] =
          bitmaps.GetByAlias(wxString::Format("spinner-%d", i), false);
    }
  }
  m_timer.Start(kIntervalMs);
}

void Spinner::Stop() { m_timer.Stop(); }

void Spinner::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  m_frame = (m_frame + 1) % kFrameCount;
  if (m_callback) {
    m_callback(s_frames[m_frame]);
  }
}
