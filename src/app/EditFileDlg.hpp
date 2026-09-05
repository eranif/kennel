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

protected:
  // Writes the editor contents back to m_filePath. Bound to the Save tool.
  void OnSave(wxCommandEvent &evt);
  // Enables/disables the Save tool based on whether the editor is dirty.
  void OnSaveUpdateUI(wxUpdateUIEvent &evt);

  Editor *m_editor{nullptr};
};

class ReadOnlyFileViewer : public EditFileDlg {
public:
  ReadOnlyFileViewer(wxWindow *parent, const wxTerminalTheme &theme)
      : EditFileDlg(parent, theme) {
    // No need for the toolbar for a read-only viewer.
    GetSizer()->Detach(m_toolbar);
    m_toolbar->Destroy();
    m_toolbar = nullptr;

    SetEditable(false);
    GetSizer()->Layout();

    m_editor->GetCtrl()->Bind(wxEVT_KEY_DOWN, &ReadOnlyFileViewer::OnKeyDown,
                              this);
  }
  ~ReadOnlyFileViewer() override = default;

protected:
  void OnKeyDown(wxKeyEvent &event) {
    if (event.GetKeyCode() != WXK_ESCAPE) {
      event.Skip();
      return;
    }
    EndModal(wxID_OK);
  }
};