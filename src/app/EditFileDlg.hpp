#pragma once

#include "UI.hpp"
#include "app/Editor.h"
#include "terminal_theme.h"

class EditFileDlg : public EditFileDlgBase {
public:
  EditFileDlg(wxWindow *parent, const wxTerminalTheme &theme);
  ~EditFileDlg() override;

  void LoadFile(const wxString &filepath);
  void LoadText(const wxString &text, EditorLang lang);
  void SetEditable(bool editable);

private:
  // Writes the editor contents back to m_filePath. Bound to the Save tool.
  void OnSave(wxCommandEvent &evt);
  // Enables/disables the Save tool based on whether the editor is dirty.
  void OnSaveUpdateUI(wxUpdateUIEvent &evt);

  Editor *m_editor{nullptr};
};
