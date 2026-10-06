#include "app/AcceleratorInterceptor.h"
#include "app/MainFrame.h"
#include "wx/app.h"
#include "wx/xrc/xmlres.h"

AcceleratorInterceptor::AcceleratorInterceptor(wxWindow *win) : m_ctrl{win} {
#if defined(__WXMSW__) || defined(__WXGTK__)
  // On Windows / GTK, we need another hook for wxEVT_CHAR_HOOK so we can
  // handle keyboard shortcuts. On macOS they are all menu accelerators.
  m_ctrl->Bind(wxEVT_CHAR_HOOK, &AcceleratorInterceptor::OnCharHook, this);
#endif
}

AcceleratorInterceptor::~AcceleratorInterceptor() {}

void AcceleratorInterceptor::OnCharHook(wxKeyEvent &keyEvent) {
  // Ctrl+Tab / Ctrl+Shift+Tab opens the page switcher.
  if (keyEvent.GetKeyCode() == WXK_TAB &&
      (keyEvent.GetModifiers() & ~wxMOD_SHIFT) == wxMOD_RAW_CONTROL) {
    wxCommandEvent evtSwitch{wxEVT_MENU, keyEvent.ShiftDown() ? wxID_BACKWARD
                                                              : wxID_FORWARD};
    GetMainFrame()->GetEventHandler()->AddPendingEvent(evtSwitch);
    return;
  } else if (keyEvent.GetKeyCode() == WXK_F2) {
    // Rename
    wxCommandEvent evtRename{wxEVT_MENU, XRCID("rename-selection")};
    wxTheApp->GetTopWindow()->GetEventHandler()->AddPendingEvent(evtRename);
    return;
  } else if (keyEvent.GetKeyCode() == WXK_F5) {
    // Rename
    wxCommandEvent evtRefreshSession{wxEVT_MENU, wxID_REFRESH};
    wxTheApp->GetTopWindow()->GetEventHandler()->AddPendingEvent(
        evtRefreshSession);
    return;
  } else if (keyEvent.GetUnicodeKey() == 'N' &&
             keyEvent.GetModifiers() == wxMOD_CONTROL) {
    wxCommandEvent evtConfigureAgent{wxEVT_MENU, wxID_NEW};
    wxTheApp->GetTopWindow()->GetEventHandler()->AddPendingEvent(
        evtConfigureAgent);
    return;
  } else if (keyEvent.GetUnicodeKey() == 'E' &&
             keyEvent.GetModifiers() == wxMOD_CONTROL) {
    wxCommandEvent evtStartTerminal{wxEVT_MENU, XRCID("start-terminal")};
    wxTheApp->GetTopWindow()->GetEventHandler()->AddPendingEvent(
        evtStartTerminal);
    return;
  } else if (keyEvent.GetUnicodeKey() == 'T' &&
             keyEvent.GetModifiers() == wxMOD_CONTROL) {
    wxCommandEvent evtStartAgent{wxEVT_MENU, XRCID("start-agent")};
    wxTheApp->GetTopWindow()->GetEventHandler()->AddPendingEvent(evtStartAgent);
    return;
  }
  keyEvent.Skip();
}
