#pragma once

#include "UI.hpp"
#include "terminal_theme.h"

class EditFileDlg : public EditFileDlgBase {
public:
  EditFileDlg(wxWindow *parent, const wxTerminalTheme &theme);
  ~EditFileDlg() override;

private:
  // Writes the editor contents back to m_filePath. Bound to the Save tool.
  void OnSave(wxCommandEvent &evt);
  // Enables/disables the Save tool based on whether the editor is dirty.
  void OnSaveUpdateUI(wxUpdateUIEvent &evt);

  wxString m_filePath; // file currently being edited (empty => nothing loaded)
};
