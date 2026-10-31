#pragma once

#include "terminal_theme.h"
#include <optional>
#include <wx/panel.h>
#include <wx/stc/stc.h>

enum class EditorLang {
  kText,
  kJson,
  kCxx,
  kJava,
  kCMake,
  kBash,
  kMarkdown,
  kXml,
  kRuby,
  kTypeScript,
  kJavaScript,
  kPython,
  kMakefile,
  kRust,
  kDiff,
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

// Sent on the EventNotifier when an editor option of the preferences changed
// (highlight braces, highlight indent lines): every editor applies it.
wxDECLARE_EVENT(wxEVT_EDITOR_OPTIONS_CHANGED, wxCommandEvent);

class Editor : public wxPanel {
public:
  Editor(wxWindow *parent, EditorLang lang = EditorLang::kText,
         const wxTerminalTheme &theme = wxTerminalTheme::MakeDarkTheme());
  ~Editor() override;

  void SetEditorLanguage(EditorLang lang);
  // Puts the caret at `line` and `column`, both from 1 (the column in bytes).
  // Out of range goes to the nearest place that exists. Unfolds the line.
  void GotoLine(int line, int column = 1);
  void SetTheme(const wxTerminalTheme &theme);
  // Wraps the long lines, or lets them run off to the right.
  void SetWrapLines(bool wrap);

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
    m_ctrl->EmptyUndoBuffer(); // Undo must not go back to an empty file
    m_filepath = filePath;
    return true;
  }

  void SetText(const wxString &text) {
    EditableLocker editable{m_ctrl};
    m_ctrl->SetText(text);
    m_ctrl->EmptyUndoBuffer();
    m_ctrl->SetModified(false);
    m_ctrl->SetSavePoint();
  }

  bool IsModified() const { return m_ctrl->IsModified(); }
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
  // The colour of the line numbers: the text colour, dimmed towards the
  // background (the theme's "bright black" can be close to the background).
  wxColour LineNumberColour() const;
  // The colour of every kind of comment: a muted green that suits a dark or a
  // light background. Both colours keep a minimum contrast with the background.
  wxColour CommentColour() const;
  void InitEditor();
  void InitJsonStyle();
  void InitTextStyle();
  void InitCppLikeStyle(const char *keywords);
  void InitCxxStyle();
  void InitJavaStyle();
  void InitCMakeStyle();
  void InitBashStyle();
  void InitMarkdownStyle();
  void InitXmlStyle();
  void InitRubyStyle();
  void InitTypeScriptStyle();
  void InitJavaScriptStyle();
  void InitPythonStyle();
  void InitMakefileStyle();
  void InitRustStyle();
  void InitDiffStyle();
  // Sizes the line-number margin to fit the current number of lines.
  void UpdateLineNumberMargin();
  void OnModified(wxStyledTextEvent &event);
  void OnUpdateUI(wxStyledTextEvent &event);
  void OnOptionsChanged(wxCommandEvent &event);
  // Applies the editor options of the preferences (the indent lines).
  void ApplyOptions();
  // Highlights the brace at the caret (or before it) and its pair, like
  // CodeLite does. A brace without a pair is marked as bad.
  void DoBraceMatching();
  void ClearBraceHighlight();

  wxStyledTextCtrl *m_ctrl{nullptr};
  EditorLang m_lang = EditorLang::kText;
  wxTerminalTheme m_theme;
  std::optional<wxString> m_filepath{std::nullopt};
  long m_lastUpdatePosition{wxNOT_FOUND};
  bool m_hasBraceHighlight{false}; // A good or a bad one is shown
};
