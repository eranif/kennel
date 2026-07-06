#include "EditFileDlg.hpp"
#include "core/AppManager.h"
#include "terminal_theme.h"

#include <wx/accel.h>
#include <wx/artprov.h>
#include <wx/msgdlg.h>

namespace {
void AddProperty(wxStyledTextCtrl *ctrl, int style, const wxColour &bg,
                 const wxColour &fg) {
  ctrl->StyleSetForeground(style, fg);
  ctrl->StyleSetBackground(style, bg);
}

void InitJsonStyle(wxStyledTextCtrl *ctrl, const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_JSON);
  ctrl->StyleClearAll();
  ctrl->FoldDisplayTextSetStyle(wxSTC_FOLDDISPLAYTEXT_BOXED);
  ctrl->SetIdleStyling(wxSTC_IDLESTYLING_TOVISIBLE);
  ctrl->SetTechnology(wxSTC_TECHNOLOGY_DIRECTWRITE);

  // Find the default style
  for (int i = 0; i < wxSTC_STYLE_MAX; ++i) {
    ctrl->StyleSetBackground(i, theme.bg);
    ctrl->StyleSetForeground(i, theme.fg);
    ctrl->StyleSetFont(i, theme.font);
  }

  AddProperty(ctrl, wxSTC_JSON_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_JSON_NUMBER, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_JSON_STRING, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_JSON_STRINGEOL, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_JSON_PROPERTYNAME, theme.bg, theme.brightBlue);
  AddProperty(ctrl, wxSTC_JSON_ESCAPESEQUENCE, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_JSON_LINECOMMENT, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_JSON_BLOCKCOMMENT, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_JSON_OPERATOR, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_JSON_URI, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_JSON_COMPACTIRI, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_JSON_KEYWORD, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_JSON_LDKEYWORD, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_JSON_ERROR, theme.bg, theme.red);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, theme.brightBlack);
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);

  // Indentation
  ctrl->SetUseTabs(false);
  ctrl->SetTabWidth(2);
  ctrl->SetIndent(2);
  ctrl->SetLayoutCache(wxSTC_CACHE_PAGE);
}

} // namespace

EditFileDlg::EditFileDlg(wxWindow *parent, const wxTerminalTheme &theme)
    : EditFileDlgBase(parent) {
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

  InitJsonStyle(m_stcEditor, theme);
  wxFileName config_file = AppManager::Get().Paths().ConfigFile();
  if (!config_file.FileExists()) {
    return;
  }

  m_filePath = config_file.GetFullPath();
  m_stcEditor->LoadFile(m_filePath);
  m_stcEditor->SetModified(false);
  m_stcEditor->CallAfter(&wxStyledTextCtrl::SetFocus);
  SetLabel(wxString() << _("Editing file: ") << m_filePath);
  CentreOnParent();
}

EditFileDlg::~EditFileDlg() {}

void EditFileDlg::OnSave(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  if (m_filePath.empty()) {
    return;
  }
  if (!m_stcEditor->SaveFile(m_filePath)) {
    wxMessageBox(wxString() << _("Could not write file:\n") << m_filePath,
                 "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }
  m_stcEditor->SetModified(false);

  // Re-read the saved config into the running app (rebuilds the adapter
  // registry). On a parse error the old in-memory config is kept; report it.
  if (Status st = AppManager::Get().Reload(); !st.ok()) {
    wxMessageBox(wxString()
                     << _("Saved, but the config could not be applied:\n")
                     << st.message()
                     << _("\n\nThe previous settings remain in effect."),
                 "Kennel", wxOK | wxICON_WARNING, this);
  }
}

void EditFileDlg::OnSaveUpdateUI(wxUpdateUIEvent &evt) {
  evt.Enable(!m_filePath.empty() && m_stcEditor->IsModified());
}
