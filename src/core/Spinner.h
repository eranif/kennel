#pragma once

#include <array>
#include <functional>
#include <wx/bmpbndl.h>
#include <wx/event.h>
#include <wx/timer.h>

// A turning spinner: the timer and the frames. While it runs, the callback is
// called for each new frame (every 50 ms); the owner draws it where it wants.
// The frames are the bitmaps with the aliases spinner-0 ... spinner-15. The
// same timer also drives the pulse of a blocked page (blocked-0 ... blocked-15:
// an orange dot that blinks inside a fading outline).
class Spinner : public wxEvtHandler {
public:
  using SpinnerDrawCB = std::function<void(const wxBitmapBundle &)>;
  static constexpr int kFrameCount = 16;

  explicit Spinner(SpinnerDrawCB cb);
  ~Spinner() override;

  // Does nothing when the spinner runs (or does not run) already.
  void Start();
  void Stop();
  bool IsRunning() const { return m_timer.IsRunning(); }

  // The frame to draw now.
  int GetFrame() const { return m_frame; }
  const wxBitmapBundle &GetBitmap() const { return s_frames[m_frame]; }
  // The frame of the pulse to draw now (not ok before the first Start()).
  const wxBitmapBundle &GetPulseBitmap() const { return s_pulse[m_frame]; }

private:
  void OnTimer(wxTimerEvent &event);

  static constexpr int kIntervalMs = 50;

  SpinnerDrawCB m_callback;
  wxTimer m_timer;
  // Shared by all spinners and loaded once, on the first Start() (the owner can
  // be made before the bitmaps are loaded). Everything runs on the main thread.
  static inline std::array<wxBitmapBundle, kFrameCount> s_frames;
  static inline std::array<wxBitmapBundle, kFrameCount> s_pulse;
  int m_frame{0};
};
