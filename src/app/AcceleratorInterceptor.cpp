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
  } else if (keyEvent.GetKeyCode() == WXK_F3 &&
             (keyEvent.GetModifiers() & ~wxMOD_SHIFT) == wxMOD_NONE &&
             GetMainFrame()->GetMainView()->GetSessionsBook()->CanFindNext()) {
    // Find Next / Find Previous. A terminal without a search keeps F3 for the
    // program in it.
    wxCommandEvent evtFindNext{wxEVT_MENU, keyEvent.ShiftDown()
                                               ? XRCID("find-previous")
                                               : XRCID("find-next")};
    GetMainFrame()->GetEventHandler()->AddPendingEvent(evtFindNext);
    return;
  } else if (keyEvent.GetUnicodeKey() == 'F' &&
             keyEvent.GetModifiers() == wxMOD_CONTROL) {
    // Find, in the terminal too (the terminal does not get Ctrl+F).
    wxCommandEvent evtFind{wxEVT_MENU, wxID_FIND};
    GetMainFrame()->GetEventHandler()->AddPendingEvent(evtFind);
    return;
#ifndef __WXOSX__
  } else if (keyEvent.GetUnicodeKey() == 'G' &&
             keyEvent.GetModifiers() == wxMOD_CONTROL &&
             GetMainFrame()->GetMainView()->GetSessionsBook()->CanGotoLine()) {
    // Go to Line. A terminal keeps Ctrl+G (the bell) for the program in it.
    wxCommandEvent evtGoto{wxEVT_MENU, XRCID("goto-line")};
    GetMainFrame()->GetEventHandler()->AddPendingEvent(evtGoto);
    return;
#endif
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
