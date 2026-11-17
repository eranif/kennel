#pragma once

#include "terminal_theme.h"
#include <optional>
#include <vector>
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
  // Out of range goes to the nearest place that exists. Unfolds the line. With
  // a `length` (more than 0) that many characters from the column are selected
  // (not past the end of the line), and then the column and the length count
  // characters, not bytes.
  void GotoLine(int line, int column = 1, int length = 0);
  void SetTheme(const wxTerminalTheme &theme);
  // Wraps the long lines, or lets them run off to the right.
  void SetWrapLines(bool wrap);

  // Bookmarks: a marked line shows a mark in the margin at the left of the line
  // numbers. They are not kept when the file is closed.
  // Marks the line of the caret, or takes the mark off.
  void ToggleBookmark();
  // Moves the caret to the start of the next (or previous) marked line, from
  // the line of the caret, and wraps around. False if no line is marked.
  bool GotoBookmark(bool forward);
  void ClearBookmarks();
  bool HasBookmarks() const;
  // The marked lines (from 0), first to last.
  std::vector<int> GetBookmarks() const;
  // Replaces the bookmarks with these lines; a line that does not exist is
  // dropped.
  void SetBookmarks(const std::vector<int> &lines);

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

  // Writes the text to `path`, and makes it the file of the editor. On failure
  // the editor keeps the file it had.
  bool SaveAs(const wxString &path) {
    if (!m_ctrl->IsEditable() || !m_ctrl->SaveFile(path)) {
      return false;
    }
    m_ctrl->SetSavePoint();
    m_ctrl->SetModified(false);
    m_filepath = path;
    return true;
  }

  wxString GetFile() const {
    if (m_filepath)
      return *m_filepath;
    return wxEmptyString;
  }

  // Gives `ctrl` the look of an editor: margins, caret, the colours of `theme`
  // and the highlighting of `lang`. For any wxStyledTextCtrl, not only the one
  // of an Editor.
  static void InitEditor(wxStyledTextCtrl *ctrl, EditorLang lang,
                         const wxTerminalTheme &theme);

private:
  // The margins of the editor, left to right, and the marker of the bookmarks.
  static constexpr int kBookmarkMargin = 0;
  static constexpr int kLineNumberMargin = 1;
  static constexpr int kSeparatorMargin = 2;
  static constexpr int kBookmarkMarker = 1; // 25 and up are for folding
  // The indicator of the highlighted word (0 to 7 are for the lexers).
  static constexpr int kWordIndicator = 9;
  // A longer selection is not a word; the most places marked in one pass.
  static constexpr size_t kMaxHighlightedWord = 256;
  static constexpr int kMaxHighlights = 2000;

  void InitEditor() {
    InitEditor(m_ctrl, m_lang, m_theme);
    InitBookmarks(m_ctrl, m_theme);
    InitWordHighlight(m_ctrl, m_theme);
  }
  // The look of the mark of the highlighted word, in the colours of `theme`.
  static void InitWordHighlight(wxStyledTextCtrl *ctrl,
                                const wxTerminalTheme &theme);
  // Marks, with an indicator, the other places of the selected word in the
  // lines that are shown (Highlight Word): the same text, the case counts. Done
  // when the application is idle after the selection, the text or the scroll
  // changed, so that typing and scrolling stay fast.
  void OnIdle(wxIdleEvent &event);
  void HighlightWords();
  // The margin and the mark of the bookmarks, in the colours of `theme`.
  static void InitBookmarks(wxStyledTextCtrl *ctrl,
                            const wxTerminalTheme &theme);

  static void AddProperty(wxStyledTextCtrl *ctrl, int style, const wxColour &bg,
                          const wxColour &fg);
  // The colour of the line numbers: the text colour, dimmed towards the
  // background (the theme's "bright black" can be close to the background).
  static wxColour LineNumberColour(const wxTerminalTheme &theme);
  // The colour of every kind of comment: a muted green that suits a dark or a
  // light background. Both colours keep a minimum contrast with the background.
  static wxColour CommentColour(const wxTerminalTheme &theme);
  static void InitJsonStyle(wxStyledTextCtrl *ctrl,
                            const wxTerminalTheme &theme);
  static void InitTextStyle(wxStyledTextCtrl *ctrl,
                            const wxTerminalTheme &theme);
  static void InitCppLikeStyle(wxStyledTextCtrl *ctrl,
                               const wxTerminalTheme &theme,
                               const char *keywords);
  static void InitCxxStyle(wxStyledTextCtrl *ctrl,
                           const wxTerminalTheme &theme);
  static void InitJavaStyle(wxStyledTextCtrl *ctrl,
                            const wxTerminalTheme &theme);
  static void InitCMakeStyle(wxStyledTextCtrl *ctrl,
                             const wxTerminalTheme &theme);
  static void InitBashStyle(wxStyledTextCtrl *ctrl,
                            const wxTerminalTheme &theme);
  static void InitMarkdownStyle(wxStyledTextCtrl *ctrl,
                                const wxTerminalTheme &theme);
  static void InitXmlStyle(wxStyledTextCtrl *ctrl,
                           const wxTerminalTheme &theme);
  static void InitRubyStyle(wxStyledTextCtrl *ctrl,
                            const wxTerminalTheme &theme);
  static void InitTypeScriptStyle(wxStyledTextCtrl *ctrl,
                                  const wxTerminalTheme &theme);
  static void InitJavaScriptStyle(wxStyledTextCtrl *ctrl,
                                  const wxTerminalTheme &theme);
  static void InitPythonStyle(wxStyledTextCtrl *ctrl,
                              const wxTerminalTheme &theme);
  static void InitMakefileStyle(wxStyledTextCtrl *ctrl,
                                const wxTerminalTheme &theme);
  static void InitRustStyle(wxStyledTextCtrl *ctrl,
                            const wxTerminalTheme &theme);
  static void InitDiffStyle(wxStyledTextCtrl *ctrl,
                            const wxTerminalTheme &theme);
  // Sizes the line-number margin to fit the current number of lines.
  static void UpdateLineNumberMargin(wxStyledTextCtrl *ctrl);
  void OnModified(wxStyledTextEvent &event);
  void OnUpdateUI(wxStyledTextEvent &event);
  void OnOptionsChanged(wxCommandEvent &event);
  // Applies the editor options of the preferences (the indent lines).
  static void ApplyOptions(wxStyledTextCtrl *ctrl);
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
  bool m_wordsDirty{false};        // HighlightWords() has to run
};
