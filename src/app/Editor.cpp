#include "app/Editor.h"
#include "wx/sizer.h"

Editor::Editor(wxWindow *parent, EditorLang lang)
    : wxPanel(parent), m_lang{lang} {
  SetSizer(new wxBoxSizer(wxVERTICAL));
  m_ctrl = new wxStyledTextCtrl(this);
  GetSizer()->Add(m_ctrl, wxSizerFlags(1).Expand());
  GetSizer()->Fit(this);
  Layout();

  InitEditor(wxTerminalTheme::MakeDarkTheme());
}

Editor::~Editor() {}

void Editor::AddProperty(int style, const wxColour &bg, const wxColour &fg) {
  m_ctrl->StyleSetForeground(style, fg);
  m_ctrl->StyleSetBackground(style, bg);
}

void Editor::InitEditor(const wxTerminalTheme &theme) {
  m_ctrl->StyleClearAll();
  m_ctrl->FoldDisplayTextSetStyle(wxSTC_FOLDDISPLAYTEXT_BOXED);
  m_ctrl->SetIdleStyling(wxSTC_IDLESTYLING_TOVISIBLE);
  m_ctrl->SetTechnology(wxSTC_TECHNOLOGY_DIRECTWRITE);

  // Find the default style
  for (int i = 0; i < wxSTC_STYLE_MAX; ++i) {
    m_ctrl->StyleSetBackground(i, theme.bg);
    m_ctrl->StyleSetForeground(i, theme.fg);
    m_ctrl->StyleSetFont(i, theme.font);
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
    InitTextStyle(theme);
    break;
  case EditorLang::kCxx:
    InitCxxStyle(theme);
    break;
  case EditorLang::kJson:
    InitJsonStyle(theme);
    break;
  }
}

void Editor::InitTextStyle(const wxTerminalTheme &theme) {
  // Plain text: no syntax highlighting, just the default colours.
  m_ctrl->SetLexer(wxSTC_LEX_NULL);
  AddProperty(wxSTC_STYLE_DEFAULT, theme.bg, theme.fg);
  AddProperty(wxSTC_STYLE_LINENUMBER, theme.bg, theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitCxxStyle(const wxTerminalTheme &theme) {
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

  AddProperty(wxSTC_C_DEFAULT, theme.bg, theme.fg);
  AddProperty(wxSTC_C_COMMENT, theme.bg, theme.brightBlack);
  AddProperty(wxSTC_C_COMMENTLINE, theme.bg, theme.brightBlack);
  AddProperty(wxSTC_C_COMMENTDOC, theme.bg, theme.brightBlack);
  AddProperty(wxSTC_C_COMMENTLINEDOC, theme.bg, theme.brightBlack);
  AddProperty(wxSTC_C_COMMENTDOCKEYWORD, theme.bg, theme.cyan);
  AddProperty(wxSTC_C_COMMENTDOCKEYWORDERROR, theme.bg, theme.red);
  AddProperty(wxSTC_C_NUMBER, theme.bg, theme.green);
  AddProperty(wxSTC_C_WORD, theme.bg, theme.magenta);
  AddProperty(wxSTC_C_WORD2, theme.bg, theme.blue);
  AddProperty(wxSTC_C_STRING, theme.bg, theme.yellow);
  AddProperty(wxSTC_C_STRINGEOL, theme.bg, theme.yellow);
  AddProperty(wxSTC_C_STRINGRAW, theme.bg, theme.yellow);
  AddProperty(wxSTC_C_CHARACTER, theme.bg, theme.yellow);
  AddProperty(wxSTC_C_HASHQUOTEDSTRING, theme.bg, theme.yellow);
  AddProperty(wxSTC_C_VERBATIM, theme.bg, theme.yellow);
  AddProperty(wxSTC_C_ESCAPESEQUENCE, theme.bg, theme.yellow);
  AddProperty(wxSTC_C_PREPROCESSOR, theme.bg, theme.cyan);
  AddProperty(wxSTC_C_PREPROCESSORCOMMENT, theme.bg, theme.brightBlack);
  AddProperty(wxSTC_C_PREPROCESSORCOMMENTDOC, theme.bg, theme.brightBlack);
  AddProperty(wxSTC_C_OPERATOR, theme.bg, theme.fg);
  AddProperty(wxSTC_C_IDENTIFIER, theme.bg, theme.fg);
  AddProperty(wxSTC_C_UUID, theme.bg, theme.green);
  AddProperty(wxSTC_C_REGEX, theme.bg, theme.yellow);
  AddProperty(wxSTC_C_USERLITERAL, theme.bg, theme.green);
  AddProperty(wxSTC_C_TASKMARKER, theme.bg, theme.brightYellow);
  AddProperty(wxSTC_STYLE_LINENUMBER, theme.bg, theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitJsonStyle(const wxTerminalTheme &theme) {
  m_ctrl->SetLexer(wxSTC_LEX_JSON);

  // JSON literal keywords.
  m_ctrl->SetKeyWords(0, "true false null");

  AddProperty(wxSTC_JSON_DEFAULT, theme.bg, theme.fg);
  AddProperty(wxSTC_JSON_NUMBER, theme.bg, theme.green);
  AddProperty(wxSTC_JSON_STRING, theme.bg, theme.yellow);
  AddProperty(wxSTC_JSON_STRINGEOL, theme.bg, theme.yellow);
  AddProperty(wxSTC_JSON_PROPERTYNAME, theme.bg, theme.brightBlue);
  AddProperty(wxSTC_JSON_ESCAPESEQUENCE, theme.bg, theme.yellow);
  AddProperty(wxSTC_JSON_LINECOMMENT, theme.bg, theme.cyan);
  AddProperty(wxSTC_JSON_BLOCKCOMMENT, theme.bg, theme.cyan);
  AddProperty(wxSTC_JSON_OPERATOR, theme.bg, theme.fg);
  AddProperty(wxSTC_JSON_URI, theme.bg, theme.blue);
  AddProperty(wxSTC_JSON_COMPACTIRI, theme.bg, theme.blue);
  AddProperty(wxSTC_JSON_KEYWORD, theme.bg, theme.magenta);
  AddProperty(wxSTC_JSON_LDKEYWORD, theme.bg, theme.magenta);
  AddProperty(wxSTC_JSON_ERROR, theme.bg, theme.red);
  AddProperty(wxSTC_STYLE_LINENUMBER, theme.bg, theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::ApplyTheme(const wxTerminalTheme &theme) { InitEditor(theme); }