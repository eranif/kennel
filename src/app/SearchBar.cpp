#include "app/SearchBar.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

wxDEFINE_EVENT(wxEVT_SEARCHBAR_FIND, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SEARCHBAR_REPLACE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SEARCHBAR_REPLACE_ALL, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SEARCHBAR_CLOSE, wxCommandEvent);

#ifdef __WXGTK__
constexpr long kSearchBarStyle = wxBORDER_NONE | wxTAB_TRAVERSAL;
#else
constexpr long kSearchBarStyle = wxBORDER_DEFAULT | wxTAB_TRAVERSAL;
#endif
constexpr int kButtonMargin = 10;

SearchBar::SearchBar(wxWindow *parent, Kind kind) {
  wxPanel::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                  kSearchBarStyle);
  const bool editor = kind == Kind::kEditor;
  const wxSizerFlags flags = wxSizerFlags(0).Expand().Border(wxALL, 2);

  // Measure the width a button.
  wxSize buttonSize{
      parent->GetTextExtent(editor ? _("Replace All") : _("Previous"))
              .GetWidth() +
          (parent->FromDIP(kButtonMargin) * 2),
      wxNOT_FOUND};

  // [ find what    ][ down    ][ up          ][ full word ][ case sensitive ]
  // [ replace with ][ replace ][ replace all ][           ][                ]
  // A terminal has the first three of the first line only. The texts take the
  // width that is left.
  auto *sizer = new wxFlexGridSizer(editor ? 2 : 1, editor ? 5 : 3, 0, 0);
  sizer->AddGrowableCol(0);
  sizer->SetFlexibleDirection(wxBOTH);

  m_find = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                          wxDefaultSize, wxTE_PROCESS_ENTER);
  m_find->SetHint(_("Find"));
  // For the mouse: the same as Enter and Shift+Enter.
  auto *next =
      new wxButton(this, wxID_ANY, _("Next"), wxDefaultPosition, buttonSize);
  next->SetToolTip(_("Find the next match (Enter)"));
  next->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    Send(wxEVT_SEARCHBAR_FIND, kForward);
  });
  auto *previous = new wxButton(this, wxID_ANY, _("Previous"),
                                wxDefaultPosition, buttonSize);
  previous->SetToolTip(_("Find the previous match (Shift+Enter)"));
  previous->Bind(wxEVT_BUTTON,
                 [this](wxCommandEvent &) { Send(wxEVT_SEARCHBAR_FIND, 0); });
  sizer->Add(m_find, flags);
  sizer->Add(next, flags);
  sizer->Add(previous, flags);

  if (editor) {
    m_wholeWord = new wxCheckBox(this, wxID_ANY, _("Full word"));
    m_matchCase = new wxCheckBox(this, wxID_ANY, _("Case sensitive"));
    auto onOption = [this](wxCommandEvent &) {
      Send(wxEVT_SEARCHBAR_FIND, kIncremental);
    };
    m_wholeWord->Bind(wxEVT_CHECKBOX, onOption);
    m_matchCase->Bind(wxEVT_CHECKBOX, onOption);
    sizer->Add(m_wholeWord, flags);
    sizer->Add(m_matchCase, flags);

    m_replace = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_PROCESS_ENTER);
    m_replace->SetHint(_("Replace with"));
    // Tab goes from the find text to the replace text (see OnCharHook()).
    m_replace->MoveAfterInTabOrder(m_find);
    m_replaceButton = new wxButton(this, wxID_ANY, _("Replace"),
                                   wxDefaultPosition, buttonSize);
    m_replaceButton->SetToolTip(
        _("Replace the selected match and find the next one (Enter)"));
    m_replaceButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      Send(wxEVT_SEARCHBAR_REPLACE, kForward);
    });
    m_replaceAllButton = new wxButton(this, wxID_ANY, _("Replace all"),
                                      wxDefaultPosition, buttonSize);
    m_replaceAllButton->SetToolTip(_("Replace all the matches in the file"));
    m_replaceAllButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      Send(wxEVT_SEARCHBAR_REPLACE_ALL, kForward);
    });
    sizer->Add(m_replace, flags);
    sizer->Add(m_replaceButton, flags);
    sizer->Add(m_replaceAllButton, flags);
    sizer->AddStretchSpacer();
    sizer->AddStretchSpacer();

    m_next = next;
    m_previous = previous;
    UpdateButtonWidths();
    // The best size changes with the scale of the monitor.
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent &event) {
      event.Skip();
      UpdateButtonWidths();
    });
  }
  SetSizer(sizer);

  m_find->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
    Send(wxEVT_SEARCHBAR_FIND, kIncremental);
  });
  // wxEVT_CHAR_HOOK goes up from the window with the focus: every control of
  // the bar.
  Bind(wxEVT_CHAR_HOOK, &SearchBar::OnCharHook, this);
}

void SearchBar::UpdateButtonWidths() {
  if (m_replaceAllButton == nullptr) {
    return;
  }
  // All the buttons are as wide as the widest one, "Replace all".
  for (wxButton *button : {m_next, m_previous, m_replaceButton}) {
    button->SetMinSize(wxDefaultSize);
  }
  const int width = m_replaceAllButton->GetBestSize().GetWidth();
  for (wxButton *button : {m_next, m_previous, m_replaceButton}) {
    button->SetMinSize(wxSize(width, -1));
  }
  Layout();
}

void SearchBar::Activate(const wxString &text) {
  if (!text.empty()) {
    m_find->ChangeValue(text);
  }
  m_find->SetFocus();
  m_find->SelectAll();
}

wxString SearchBar::GetText() const { return m_find->GetValue(); }

wxString SearchBar::GetReplaceText() const {
  return m_replace != nullptr ? m_replace->GetValue() : wxString{};
}

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

void SearchBar::EnableReplace(bool enable) {
  for (wxWindow *win : {static_cast<wxWindow *>(m_replace),
                        static_cast<wxWindow *>(m_replaceButton),
                        static_cast<wxWindow *>(m_replaceAllButton)}) {
    if (win != nullptr) {
      win->Enable(enable);
    }
  }
}

void SearchBar::Send(wxEventType type, int flags) {
  wxCommandEvent event(type, GetId());
  event.SetEventObject(this);
  event.SetString(m_find->GetValue());
  event.SetInt(flags | GetOptions());
  ProcessWindowEvent(event);
}

bool SearchBar::HasFocusIn(const wxWindow *ctrl) {
  wxWindow *focus = wxWindow::FindFocus();
  return ctrl != nullptr && focus != nullptr &&
         (focus == ctrl || ctrl->IsDescendant(focus));
}

void SearchBar::OnCharHook(wxKeyEvent &event) {
  const int key = event.GetKeyCode();
  if (key == WXK_ESCAPE) {
    wxCommandEvent close(wxEVT_SEARCHBAR_CLOSE, GetId());
    close.SetEventObject(this);
    ProcessWindowEvent(close);
    return;
  }
  // Tab in the replace text goes back to the find text.
  if (key == WXK_TAB && event.GetModifiers() == wxMOD_NONE &&
      HasFocusIn(m_replace)) {
    m_find->SetFocus();
    m_find->SelectAll();
    return;
  }
  // Enter in a text: the buttons and the checkboxes keep theirs.
  if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
    const int modifiers = event.GetModifiers();
    if (HasFocusIn(m_find)) {
      if (modifiers == wxMOD_NONE) {
        Send(wxEVT_SEARCHBAR_FIND, kForward);
        return;
      }
      if (modifiers == wxMOD_SHIFT) {
        Send(wxEVT_SEARCHBAR_FIND, 0);
        return;
      }
    } else if (HasFocusIn(m_replace) && modifiers == wxMOD_NONE) {
      Send(wxEVT_SEARCHBAR_REPLACE, kForward);
      return;
    }
  }
  event.Skip();
}
