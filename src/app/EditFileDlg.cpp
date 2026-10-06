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

EditorLang EditFileDlg::LangFromPath(const wxString &filepath) {
  // Works for local (either separator) and remote (POSIX) paths alike.
  const wxString name = filepath.AfterLast('/').AfterLast('\\');

  static const std::unordered_map<wxString, EditorLang> nameMap{
      {"cmakelists.txt", EditorLang::kCMake},
      {".bashrc", EditorLang::kBash},
      {".bash_profile", EditorLang::kBash},
      {".bash_aliases", EditorLang::kBash},
      {".profile", EditorLang::kBash},
      {".zshrc", EditorLang::kBash},
      {".zprofile", EditorLang::kBash},
      {"makefile", EditorLang::kMakefile},
      {"gnumakefile", EditorLang::kMakefile},
      {"rakefile", EditorLang::kRuby},
      {"gemfile", EditorLang::kRuby},
      {"guardfile", EditorLang::kRuby},
      {"vagrantfile", EditorLang::kRuby},
  };
  const wxString lowerName = name.Lower();
  if (auto it = nameMap.find(lowerName); it != nameMap.end()) {
    return it->second;
  }

  // A name with no dot has no extension (AfterLast would return it whole).
  if (!lowerName.Contains(".")) {
    return EditorLang::kText;
  }
  static const std::unordered_map<wxString, EditorLang> extMap{
      {"cpp", EditorLang::kCxx},        {"c", EditorLang::kCxx},
      {"cc", EditorLang::kCxx},         {"cxx", EditorLang::kCxx},
      {"h", EditorLang::kCxx},          {"hpp", EditorLang::kCxx},
      {"hxx", EditorLang::kCxx},        {"json", EditorLang::kJson},
      {"java", EditorLang::kJava},      {"cmake", EditorLang::kCMake},
      {"sh", EditorLang::kBash},        {"bash", EditorLang::kBash},
      {"zsh", EditorLang::kBash},       {"ksh", EditorLang::kBash},
      {"md", EditorLang::kMarkdown},    {"markdown", EditorLang::kMarkdown},
      {"xml", EditorLang::kXml},        {"xsd", EditorLang::kXml},
      {"xsl", EditorLang::kXml},        {"xslt", EditorLang::kXml},
      {"svg", EditorLang::kXml},        {"plist", EditorLang::kXml},
      {"xrc", EditorLang::kXml},        {"pom", EditorLang::kXml},
      {"rb", EditorLang::kRuby},        {"rake", EditorLang::kRuby},
      {"gemspec", EditorLang::kRuby},   {"ru", EditorLang::kRuby},
      {"ts", EditorLang::kTypeScript},  {"tsx", EditorLang::kTypeScript},
      {"mts", EditorLang::kTypeScript}, {"cts", EditorLang::kTypeScript},
      {"js", EditorLang::kJavaScript},  {"jsx", EditorLang::kJavaScript},
      {"mjs", EditorLang::kJavaScript}, {"cjs", EditorLang::kJavaScript},
      {"py", EditorLang::kPython},      {"pyw", EditorLang::kPython},
      {"pyi", EditorLang::kPython},     {"mk", EditorLang::kMakefile},
      {"mak", EditorLang::kMakefile},
  };
  return find_or(extMap, lowerName.AfterLast('.'), EditorLang::kText);
}

void EditFileDlg::LoadFile(const wxString &filepath) {
  m_editor->LoadFile(filepath);
  m_editor->SetEditorLanguage(LangFromPath(filepath));
}

void EditFileDlg::LoadText(const wxString &text, EditorLang lang) {
  m_editor->SetText(text);
  m_editor->SetEditorLanguage(lang);
}
