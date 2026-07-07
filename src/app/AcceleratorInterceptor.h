#pragma once

#include <wx/event.h>
#include <wx/window.h>

class AcceleratorInterceptor {
public:
  AcceleratorInterceptor(wxWindow *ctrl, bool isPlainTerminal);
  ~AcceleratorInterceptor();

protected:
  void OnCharHook(wxKeyEvent &keyEvent);

private:
  wxWindow *m_ctrl{nullptr};
  bool m_plainTerminal{false};
};
