#pragma once

#include "terminal_theme.h"
#include <optional>
#include <wx/panel.h>
#include <wx/stc/stc.h>

enum class EditorLang {
  kText,
  kJson,
  kCxx,
};

struct EditableLocker {
  wxStyledTextCtrl *m_ctrl{nullptr};
  bool m_oldState{true};
  EditableLocker(wxStyledTextCtrl *ctrl)
      : m_ctrl{ctrl}, m_oldState{ctrl->IsEditable()} {
    m_ctrl->SetEditable(true);
  }
  ~EditableLocker() { m_ctrl->SetEditable(m_oldState); }
};

class Editor : public wxPanel {
public:
  Editor(wxWindow *parent, EditorLang lang = EditorLang::kText,
         const wxTerminalTheme &theme = wxTerminalTheme::MakeDarkTheme());
  ~Editor() override;

  void SetEditorLanguage(EditorLang lang);
  void SetTheme(const wxTerminalTheme &theme);

  wxStyledTextCtrl *GetCtrl() { return m_ctrl; }
  bool IsEditable() const { return m_ctrl->IsEditable(); }
  void SetEditable(bool editable) { m_ctrl->SetEditable(editable); }
  bool LoadFile(const wxString &filePath) {
    EditableLocker editable{m_ctrl};
    if (!m_ctrl->LoadFile(filePath)) {
      return false;
    }
    m_ctrl->SetSavePoint();
    m_ctrl->SetModified(false);
    m_filepath = filePath;
    return true;
  }

  void SetText(const wxString &text) {
    EditableLocker editable{m_ctrl};
    m_ctrl->SetText(text);
    m_ctrl->SetModified(false);
    m_ctrl->SetSavePoint();
  }

  wxString GetText() const { return m_ctrl->GetText(); }
  bool CanSave() const {
    return m_filepath.has_value() && m_ctrl->IsEditable();
  }

  bool Save() {
    if (!CanSave())
      return false;
    if (m_ctrl->SaveFile(*m_filepath)) {
      m_ctrl->SetSavePoint();
      m_ctrl->SetModified(false);
      return true;
    }
    return false;
  }

  wxString GetFile() const {
    if (m_filepath)
      return *m_filepath;
    return wxEmptyString;
  }

private:
  void AddProperty(int style, const wxColour &bg, const wxColour &fg);
  void InitEditor();
  void InitJsonStyle();
  void InitTextStyle();
  void InitCxxStyle();

  wxStyledTextCtrl *m_ctrl{nullptr};
  EditorLang m_lang = EditorLang::kText;
  wxTerminalTheme m_theme;
  std::optional<wxString> m_filepath{std::nullopt};
};
