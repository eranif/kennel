#pragma once

#include <wx/event.h>
#include <wx/window.h>

class AcceleratorInterceptor {
public:
  AcceleratorInterceptor(wxWindow *ctrl);
  ~AcceleratorInterceptor();

protected:
  void OnCharHook(wxKeyEvent &keyEvent);

private:
  // The page that is shown can toggle its terminal pane (SessionPage::
  // CanToggleTerminalPane()).
  bool CanToggleSessionTerminal() const;

  wxWindow *m_ctrl{nullptr};
};
