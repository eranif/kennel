#include "EditFileDlg.hpp"
#include "core/Helpers.h"
#include "terminal_theme.h"

#include <wx/accel.h>
#include <wx/artprov.h>
#include <wx/msgdlg.h>

EditFileDlg::EditFileDlg(wxWindow *parent, const wxTerminalTheme &theme)
    : EditFileDlgBase(parent) {

  m_editor = new Editor(this, EditorLang::kText, theme);
  GetSizer()->Add(m_editor, wxSizerFlags(1).Expand().Border(wxALL, 5));

  wxRect rect = GetParent()->GetRect();
  rect.Deflate(20);
  SetSize(rect);

  // Use larger toolbar buttons than the wxCrafter default (16px). Override the
  // bitmap size at runtime (UI.cpp is generated, so not edited by hand) and
  // request the art at that size so the icon fills the cell. SetToolBitmapSize
  // and Realize() must bracket the AddTool calls.
  const wxSize toolSize(24, 24);
  m_toolbar->SetToolBitmapSize(toolSize);
  m_toolbar->AddTool(
      wxID_SAVE, _("Save"),
      wxArtProvider::GetBitmapBundle(wxART_FILE_SAVE, wxART_TOOLBAR, toolSize),
      _("Save the file"));
  m_toolbar->Realize();
  Bind(wxEVT_TOOL, &EditFileDlg::OnSave, this, wxID_SAVE);
  Bind(wxEVT_UPDATE_UI, &EditFileDlg::OnSaveUpdateUI, this, wxID_SAVE);
  // Cmd+S on macOS, Ctrl+S elsewhere (wxACCEL_CMD maps to the platform's
  // primary modifier). Routes to the same wxID_SAVE handler as the toolbar.
  wxAcceleratorEntry entries[1];
  entries[0].Set(wxACCEL_CMD, static_cast<int>('S'), wxID_SAVE);
  SetAcceleratorTable(wxAcceleratorTable(1, entries));
  Bind(wxEVT_MENU, &EditFileDlg::OnSave, this, wxID_SAVE);
  m_editor->GetCtrl()->CallAfter(&wxStyledTextCtrl::SetFocus);
  CentreOnParent();
}

EditFileDlg::~EditFileDlg() {}

void EditFileDlg::OnSave(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  if (!m_editor->CanSave())
    return;

  if (m_editor->CanSave() && !m_editor->Save()) {
    wxMessageBox(wxString()
                     << _("Could not write file:\n") << m_editor->GetFile(),
                 "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }
}

void EditFileDlg::OnSaveUpdateUI(wxUpdateUIEvent &evt) {
  evt.Enable(m_editor->CanSave());
}

void EditFileDlg::SetEditable(bool editable) {
  if (m_editor) {
    m_editor->SetEditable(editable);
  }
}

void EditFileDlg::LoadFile(const wxString &filepath) {
  m_editor->LoadFile(filepath);
  wxString ext = filepath.AfterLast('.').Lower();
  EditorLang lang{EditorLang::kText};
  static std::unordered_map<wxString, EditorLang> langMap{
      {"cpp", EditorLang::kCxx}, {"c", EditorLang::kCxx},
      {"cc", EditorLang::kCxx},  {"cxx", EditorLang::kCxx},
      {"h", EditorLang::kCxx},   {"hpp", EditorLang::kCxx},
      {"hxx", EditorLang::kCxx}, {"json", EditorLang::kJson},
  };
  m_editor->SetEditorLanguage(find_or(langMap, ext, EditorLang::kText));
}

void EditFileDlg::LoadText(const wxString &text, EditorLang lang) {
  m_editor->SetText(text);
  m_editor->SetEditorLanguage(lang);
}
