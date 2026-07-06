#pragma once

#include "terminal_theme.h"
#include "wx/panel.h"
#include "wx/stc/stc.h"

enum class EditorLang {
  kText,
  kJson,
  kCxx,
};

class Editor : public wxPanel {
public:
  Editor(wxWindow *parent, EditorLang lang);
  ~Editor() override;

  void ApplyTheme(const wxTerminalTheme &theme);

  wxStyledTextCtrl *GetCtrl() { return m_ctrl; }

private:
  void AddProperty(int style, const wxColour &bg, const wxColour &fg);
  void InitEditor(const wxTerminalTheme &theme);
  void InitJsonStyle(const wxTerminalTheme &theme);
  void InitTextStyle(const wxTerminalTheme &theme);
  void InitCxxStyle(const wxTerminalTheme &theme);

  wxStyledTextCtrl *m_ctrl{nullptr};
  EditorLang m_lang = EditorLang::kText;
};
