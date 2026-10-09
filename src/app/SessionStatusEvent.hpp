#pragma once

#include <wx/bmpbndl.h>
#include <wx/event.h>
#include <wx/string.h>

// What the status bar of the main window shows for the session page that is
// showing: the main text (GetString()), where the agent runs, the session with
// its icon, and whether a review buddy is working. A SessionPage sends it to
// itself whenever one of them changes (and when it is selected); it travels up
// the parent chain to MainFrame.
class SessionStatusEvent : public wxCommandEvent {
public:
  explicit SessionStatusEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  SessionStatusEvent(const SessionStatusEvent &) = default;

  wxEvent *Clone() const override { return new SessionStatusEvent(*this); }

  const wxString &GetHost() const { return m_host; }
  void SetHost(const wxString &host) { m_host = host; }

  // "session - agent"
  const wxString &GetSessionLabel() const { return m_sessionLabel; }
  void SetSessionLabel(const wxString &label) { m_sessionLabel = label; }

  // The agent's icon; not ok if it has none.
  const wxBitmapBundle &GetIcon() const { return m_icon; }
  void SetIcon(const wxBitmapBundle &icon) { m_icon = icon; }

  // The full text of the host and the session, for the tooltip of the status
  // bar (the fields cut long texts).
  const wxString &GetTooltip() const { return m_tooltip; }
  void SetTooltip(const wxString &tooltip) { m_tooltip = tooltip; }

  bool IsBusy() const { return m_busy; }
  void SetBusy(bool busy) { m_busy = busy; }

private:
  wxString m_host;
  wxString m_sessionLabel;
  wxString m_tooltip;
  wxBitmapBundle m_icon;
  bool m_busy{false};
};

wxDECLARE_EVENT(wxEVT_SESSION_STATUS, SessionStatusEvent);
