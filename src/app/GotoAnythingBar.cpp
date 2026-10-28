#include "app/GotoAnythingBar.hpp"

#include <wx/sizer.h>
#include <wx/textctrl.h>

wxDEFINE_EVENT(wxEVT_GOTOANYTHING_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_GOTOANYTHING_CLOSED, wxCommandEvent);

GotoAnythingBar::GotoAnythingBar(wxWindow *parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_SIMPLE | wxTAB_TRAVERSAL) {
  m_text = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                          wxDefaultSize, wxTE_PROCESS_ENTER);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_text, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  m_text->Bind(wxEVT_TEXT, &GotoAnythingBar::OnText, this);
  m_text->Bind(wxEVT_TEXT_ENTER, &GotoAnythingBar::OnEnter, this);
  m_text->Bind(wxEVT_CHAR_HOOK, &GotoAnythingBar::OnCharHook, this);
  m_text->Bind(wxEVT_KILL_FOCUS, &GotoAnythingBar::OnKillFocus, this);
  Hide();
}

void GotoAnythingBar::SetInitialText(const wxString &text) {
  m_text->ChangeValue(text); // ChangeValue(): no wxEVT_TEXT
  CallAfter([this]() {
    m_text->SetFocus();
    m_text->SetInsertionPointEnd();
  });
}

wxString GotoAnythingBar::GetText() const { return m_text->GetValue(); }

void GotoAnythingBar::CloseWith(CloseReason reason) {
  if (!IsShown()) {
    return;
  }
  const wxString text = m_text->GetValue();
  Hide();
  wxCommandEvent event(wxEVT_GOTOANYTHING_CLOSED, GetId());
  event.SetEventObject(this);
  event.SetString(text);
  event.SetInt(reason);
  ProcessWindowEvent(event);
}

void GotoAnythingBar::OnText(wxCommandEvent &) {
  wxCommandEvent changed(wxEVT_GOTOANYTHING_CHANGED, GetId());
  changed.SetEventObject(this);
  changed.SetString(m_text->GetValue());
  ProcessWindowEvent(changed);
}

void GotoAnythingBar::OnEnter(wxCommandEvent &) { CloseWith(kAccepted); }

void GotoAnythingBar::OnCharHook(wxKeyEvent &event) {
  if (event.GetKeyCode() == WXK_ESCAPE && event.GetModifiers() == wxMOD_NONE) {
    CloseWith(kCancelled);
    return;
  }
  event.Skip();
}

void GotoAnythingBar::OnKillFocus(wxFocusEvent &event) {
  event.Skip();
  // After the focus has gone: the owner may be moving it itself (Enter and
  // Escape did already hide the bar, so nothing is left to do then).
  CallAfter([this] {
    if (!m_text->HasFocus()) {
      CloseWith(kFocusLost);
    }
  });
}
