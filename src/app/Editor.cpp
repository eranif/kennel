#include "app/Editor.h"
#include "wx/sizer.h"

Editor::Editor(wxWindow *parent, EditorLang lang, const wxTerminalTheme &theme)
    : wxPanel(parent), m_lang{lang}, m_theme{theme} {
  SetSizer(new wxBoxSizer(wxVERTICAL));
  m_ctrl = new wxStyledTextCtrl(this);
  GetSizer()->Add(m_ctrl, wxSizerFlags(1).Expand());
  GetSizer()->Fit(this);
  Layout();
  InitEditor();
}

Editor::~Editor() {}

void Editor::AddProperty(int style, const wxColour &bg, const wxColour &fg) {
  m_ctrl->StyleSetForeground(style, fg);
  m_ctrl->StyleSetBackground(style, bg);
}

void Editor::InitEditor() {
  m_ctrl->StyleClearAll();
  m_ctrl->FoldDisplayTextSetStyle(wxSTC_FOLDDISPLAYTEXT_BOXED);
  m_ctrl->SetIdleStyling(wxSTC_IDLESTYLING_TOVISIBLE);
  m_ctrl->SetTechnology(wxSTC_TECHNOLOGY_DIRECTWRITE);

  // Find the default style
  for (int i = 0; i < wxSTC_STYLE_MAX; ++i) {
    m_ctrl->StyleSetBackground(i, m_theme.bg);
    m_ctrl->StyleSetForeground(i, m_theme.fg);
    m_ctrl->StyleSetFont(i, m_theme.font);
  }

  // Indentation
  m_ctrl->SetUseTabs(false);
  m_ctrl->SetTabWidth(2);
  m_ctrl->SetIndent(2);
  m_ctrl->SetLayoutCache(wxSTC_CACHE_PAGE);
  m_ctrl->SetWrapMode(wxSTC_WRAP_WORD);
  m_ctrl->SetMultipleSelection(true);
  m_ctrl->SetMultiPaste(true);
  // selection
  m_ctrl->CmdKeyAssign(wxSTC_KEY_LEFT, wxSTC_KEYMOD_CTRL | wxSTC_KEYMOD_SHIFT,
                       wxSTC_CMD_WORDPARTLEFTEXTEND);
  m_ctrl->CmdKeyAssign(wxSTC_KEY_RIGHT, wxSTC_KEYMOD_CTRL | wxSTC_KEYMOD_SHIFT,
                       wxSTC_CMD_WORDPARTRIGHTEXTEND);

  // movement
  m_ctrl->CmdKeyAssign(wxSTC_KEY_LEFT, wxSTC_KEYMOD_CTRL,
                       wxSTC_CMD_WORDPARTLEFT);
  m_ctrl->CmdKeyAssign(wxSTC_KEY_RIGHT, wxSTC_KEYMOD_CTRL,
                       wxSTC_CMD_WORDPARTRIGHT);

#ifdef __WXMAC__
  m_ctrl->CmdKeyAssign(wxSTC_KEY_DOWN, wxSTC_KEYMOD_CTRL,
                       wxSTC_CMD_DOCUMENTEND);
  m_ctrl->CmdKeyAssign(wxSTC_KEY_UP, wxSTC_KEYMOD_CTRL,
                       wxSTC_CMD_DOCUMENTSTART);

  // OSX: wxSTC_KEYMOD_CTRL => CMD key
  m_ctrl->CmdKeyAssign(wxSTC_KEY_RIGHT, wxSTC_KEYMOD_CTRL, wxSTC_CMD_LINEEND);
  m_ctrl->CmdKeyAssign(wxSTC_KEY_LEFT, wxSTC_KEYMOD_CTRL, wxSTC_CMD_HOME);

  // OSX: wxSTC_KEYMOD_META => CONTROL key
  m_ctrl->CmdKeyAssign(wxSTC_KEY_LEFT, wxSTC_KEYMOD_META,
                       wxSTC_CMD_WORDPARTLEFT);
  m_ctrl->CmdKeyAssign(wxSTC_KEY_RIGHT, wxSTC_KEYMOD_META,
                       wxSTC_CMD_WORDPARTRIGHT);
#endif
  switch (m_lang) {
  case EditorLang::kText:
    InitTextStyle();
    break;
  case EditorLang::kCxx:
    InitCxxStyle();
    break;
  case EditorLang::kJson:
    InitJsonStyle();
    break;
  }
}

void Editor::InitTextStyle() {
  // Plain text: no syntax highlighting, just the default colours.
  m_ctrl->SetLexer(wxSTC_LEX_NULL);
  AddProperty(wxSTC_STYLE_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::InitCxxStyle() {
  m_ctrl->SetLexer(wxSTC_LEX_CPP);

  // Primary C/C++ keywords.
  m_ctrl->SetKeyWords(
      0,
      "alignas alignof and and_eq asm auto bitand bitor bool break case "
      "catch char char8_t char16_t char32_t class compl concept const "
      "consteval constexpr constinit const_cast continue co_await co_return "
      "co_yield decltype default delete do double dynamic_cast else enum "
      "explicit export extern false float for friend goto if inline int long "
      "mutable namespace new noexcept not not_eq nullptr operator or or_eq "
      "private protected public register reinterpret_cast requires return "
      "short signed sizeof static static_assert static_cast struct switch "
      "template this thread_local throw true try typedef typeid typename "
      "union unsigned using virtual void volatile wchar_t while xor xor_eq "
      "override final");

  AddProperty(wxSTC_C_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_C_COMMENT, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_C_COMMENTLINE, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_C_COMMENTDOC, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_C_COMMENTLINEDOC, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_C_COMMENTDOCKEYWORD, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_C_COMMENTDOCKEYWORDERROR, m_theme.bg, m_theme.red);
  AddProperty(wxSTC_C_NUMBER, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_C_WORD, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_C_WORD2, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_C_STRING, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_C_STRINGEOL, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_C_STRINGRAW, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_C_CHARACTER, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_C_HASHQUOTEDSTRING, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_C_VERBATIM, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_C_ESCAPESEQUENCE, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_C_PREPROCESSOR, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_C_PREPROCESSORCOMMENT, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_C_PREPROCESSORCOMMENTDOC, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_C_OPERATOR, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_C_IDENTIFIER, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_C_UUID, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_C_REGEX, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_C_USERLITERAL, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_C_TASKMARKER, m_theme.bg, m_theme.brightYellow);
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::InitJsonStyle() {
  m_ctrl->SetLexer(wxSTC_LEX_JSON);

  // JSON literal keywords.
  m_ctrl->SetKeyWords(0, "true false null");

  AddProperty(wxSTC_JSON_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_JSON_NUMBER, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_JSON_STRING, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_JSON_STRINGEOL, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_JSON_PROPERTYNAME, m_theme.bg, m_theme.brightBlue);
  AddProperty(wxSTC_JSON_ESCAPESEQUENCE, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_JSON_LINECOMMENT, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_JSON_BLOCKCOMMENT, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_JSON_OPERATOR, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_JSON_URI, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_JSON_COMPACTIRI, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_JSON_KEYWORD, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_JSON_LDKEYWORD, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_JSON_ERROR, m_theme.bg, m_theme.red);
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::SetTheme(const wxTerminalTheme &theme) {
  m_theme = theme;
  InitEditor();
}

void Editor::SetEditorLanguage(EditorLang lang) {
  m_lang = lang;
  InitEditor();
}
