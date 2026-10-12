#include "app/SearchBar.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/utils.h>

wxDEFINE_EVENT(wxEVT_SEARCHBAR_FIND, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SEARCHBAR_CLOSE, wxCommandEvent);

SearchBar::SearchBar(wxWindow *parent, bool withOptions) {
  Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);

  auto *sizer = new wxBoxSizer(wxHORIZONTAL);
  m_search = new wxSearchCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              FromDIP(wxSize(300, -1)), wxTE_PROCESS_ENTER);
  m_search->ShowCancelButton(true);
  m_search->SetDescriptiveText(_("Find"));
  sizer->Add(m_search, wxSizerFlags(0).CenterVertical().Border(wxALL, 2));
  // For the mouse: the same as Shift+Enter and Enter. The labels are the
  // arrows U+2191 (up) and U+2193 (down).
  auto *previous =
      new wxButton(this, wxID_ANY, wxString(wxUniChar(0x2191)),
                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
  previous->SetToolTip(_("Find the previous match (Shift+Enter)"));
  previous->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Send(0); });
  auto *next = new wxButton(this, wxID_ANY, wxString(wxUniChar(0x2193)),
                            wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
  next->SetToolTip(_("Find the next match (Enter)"));
  next->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Send(kForward); });
  sizer->Add(previous, wxSizerFlags(0).CenterVertical().Border(wxALL, 2));
  sizer->Add(next, wxSizerFlags(0).CenterVertical().Border(wxALL, 2));
  if (withOptions) {
    m_wholeWord = new wxCheckBox(this, wxID_ANY, _("Full word"));
    m_matchCase = new wxCheckBox(this, wxID_ANY, _("Case sensitive"));
    sizer->Add(m_wholeWord, wxSizerFlags(0).CenterVertical().Border(wxALL, 4));
    sizer->Add(m_matchCase, wxSizerFlags(0).CenterVertical().Border(wxALL, 4));
    auto onOption = [this](wxCommandEvent &) { Send(kIncremental); };
    m_wholeWord->Bind(wxEVT_CHECKBOX, onOption);
    m_matchCase->Bind(wxEVT_CHECKBOX, onOption);
  }
  SetSizer(sizer);

  m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { Send(kIncremental); });
  // The cancel button empties the text; a wxEVT_TEXT may not come with it.
  m_search->Bind(wxEVT_SEARCH_CANCEL, [this](wxCommandEvent &) {
    m_search->ChangeValue(wxEmptyString);
    Send(kIncremental);
  });
  // A click on the search button (Enter is handled in OnCharHook, before the
  // control sees it).
  m_search->Bind(wxEVT_SEARCH, [this](wxCommandEvent &) {
    Send(wxGetKeyState(WXK_SHIFT) ? 0 : kForward);
  });
  // wxEVT_CHAR_HOOK goes up from the window with the focus: the search control
  // and the checkboxes.
  Bind(wxEVT_CHAR_HOOK, &SearchBar::OnCharHook, this);
}

void SearchBar::Activate(const wxString &text) {
  if (!text.empty()) {
    m_search->ChangeValue(text);
  }
  m_search->SetFocus();
  m_search->SelectAll();
}

wxString SearchBar::GetText() const { return m_search->GetValue(); }

int SearchBar::GetOptions() const {
  int options = 0;
  if (m_wholeWord != nullptr && m_wholeWord->GetValue()) {
    options |= kWholeWord;
  }
  if (m_matchCase != nullptr && m_matchCase->GetValue()) {
    options |= kMatchCase;
  }
  return options;
}

void SearchBar::SetOptions(int options) {
  if (m_wholeWord != nullptr) {
    m_wholeWord->SetValue((options & kWholeWord) != 0);
  }
  if (m_matchCase != nullptr) {
    m_matchCase->SetValue((options & kMatchCase) != 0);
  }
}

void SearchBar::Send(int flags) {
  wxCommandEvent event(wxEVT_SEARCHBAR_FIND, GetId());
  event.SetEventObject(this);
  event.SetString(m_search->GetValue());
  event.SetInt(flags | GetOptions());
  ProcessWindowEvent(event);
}

void SearchBar::OnCharHook(wxKeyEvent &event) {
  const int key = event.GetKeyCode();
  if (key == WXK_ESCAPE) {
    wxCommandEvent close(wxEVT_SEARCHBAR_CLOSE, GetId());
    close.SetEventObject(this);
    ProcessWindowEvent(close);
    return;
  }
  const bool enter = key == WXK_RETURN || key == WXK_NUMPAD_ENTER;
  // Only in the search control: Enter on a checkbox is left alone.
  wxWindow *focus = wxWindow::FindFocus();
  const bool inSearch =
      focus != nullptr && (focus == m_search || m_search->IsDescendant(focus));
  if (enter && inSearch) {
    const int modifiers = event.GetModifiers();
    if (modifiers == wxMOD_NONE) {
      Send(kForward);
      return;
    }
    if (modifiers == wxMOD_SHIFT) {
      Send(0);
      return;
    }
  }
  event.Skip();
}
