#pragma once

#include <array>
#include <functional>
#include <wx/bmpbndl.h>
#include <wx/event.h>
#include <wx/timer.h>

// A turning spinner: the timer and the frames. While it runs, the callback is
// called for each new frame (every 100 ms); the owner draws it where it wants.
// The frames are the bitmaps with the aliases spinner-0 ... spinner-7.
class Spinner : public wxEvtHandler {
public:
  using SpinnerDrawCB = std::function<void(const wxBitmapBundle &)>;
  static constexpr int kFrameCount = 8;

  explicit Spinner(SpinnerDrawCB cb);
  ~Spinner() override;

  // Does nothing when the spinner runs (or does not run) already.
  void Start();
  void Stop();
  bool IsRunning() const { return m_timer.IsRunning(); }

  // The frame to draw now.
  int GetFrame() const { return m_frame; }
  const wxBitmapBundle &GetBitmap() const { return s_frames[m_frame]; }

private:
  void OnTimer(wxTimerEvent &event);

  static constexpr int kIntervalMs = 100;

  SpinnerDrawCB m_callback;
  wxTimer m_timer;
  // Shared by all spinners and loaded once, on the first Start() (the owner can
  // be made before the bitmaps are loaded). Everything runs on the main thread.
  static inline std::array<wxBitmapBundle, kFrameCount> s_frames;
  int m_frame{0};
};
