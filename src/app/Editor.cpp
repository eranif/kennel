#include "app/Editor.h"
#include "MainFrame.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "wx/sizer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

wxDEFINE_EVENT(wxEVT_EDITOR_OPTIONS_CHANGED, wxCommandEvent);

namespace {
// Relative luminance, see https://www.w3.org/TR/WCAG21/#dfn-relative-luminance
double Luminance(const wxColour &colour) {
  auto linear = [](unsigned char channel) {
    const double value = channel / 255.0;
    return value <= 0.03928 ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * linear(colour.Red()) + 0.7152 * linear(colour.Green()) +
         0.0722 * linear(colour.Blue());
}

// Contrast ratio of two colours: 1 (the same) to 21 (black on white).
double Contrast(const wxColour &a, const wxColour &b) {
  const double la = Luminance(a);
  const double lb = Luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// `a` moved `t` (0..1) of the way towards `b`.
wxColour Mix(const wxColour &a, const wxColour &b, double t) {
  auto channel = [t](unsigned char from, unsigned char to) {
    return static_cast<unsigned char>(std::lround(from + (to - from) * t));
  };
  return wxColour(channel(a.Red(), b.Red()), channel(a.Green(), b.Green()),
                  channel(a.Blue(), b.Blue()));
}

// Lightens `colour` on a dark background (darkens it on a light one) until it
// has at least `minContrast` with `bg`.
wxColour EnsureContrast(wxColour colour, const wxColour &bg,
                        double minContrast) {
  const bool dark = IsDarkColour(bg);
  for (int i = 0; i < 30 && Contrast(colour, bg) < minContrast; ++i) {
    colour = colour.ChangeLightness(dark ? 108 : 92);
  }
  return colour;
}
} // namespace

wxColour Editor::LineNumberColour(const wxTerminalTheme &theme) {
  return EnsureContrast(Mix(theme.fg, theme.bg, 0.5), theme.bg, 3.0);
}

wxColour Editor::CommentColour(const wxTerminalTheme &theme) {
  const wxColour base = IsDarkColour(theme.bg) ? wxColour(0x6A, 0x99, 0x55)
                                               : wxColour(0x2E, 0x7D, 0x32);
  return EnsureContrast(base, theme.bg, 4.0);
}

Editor::Editor(wxWindow *parent, EditorLang lang, const wxTerminalTheme &theme)
    : wxPanel(parent), m_lang{lang}, m_theme{theme} {
  SetSizer(new wxBoxSizer(wxVERTICAL));
  m_ctrl = new wxStyledTextCtrl(this);
  GetSizer()->Add(m_ctrl, wxSizerFlags(1).Expand());
  GetSizer()->Fit(this);
  Layout();
  m_ctrl->Bind(wxEVT_STC_MODIFIED, &Editor::OnModified, this);
  m_ctrl->Bind(wxEVT_STC_UPDATEUI, &Editor::OnUpdateUI, this);
  EventNotifier::Get()->Bind(wxEVT_EDITOR_OPTIONS_CHANGED,
                             &Editor::OnOptionsChanged, this);
  InitEditor();
}

Editor::~Editor() {
  EventNotifier::Get()->Unbind(wxEVT_EDITOR_OPTIONS_CHANGED,
                               &Editor::OnOptionsChanged, this);
}

void Editor::AddProperty(wxStyledTextCtrl *ctrl, int style, const wxColour &bg,
                         const wxColour &fg) {
  ctrl->StyleSetForeground(style, fg);
  ctrl->StyleSetBackground(style, bg);
}

void Editor::InitEditor(wxStyledTextCtrl *ctrl, EditorLang lang,
                        const wxTerminalTheme &theme) {
  const auto &prefs = AppManager::Get().GetPrefs();
  ctrl->StyleClearAll();
  ctrl->FoldDisplayTextSetStyle(wxSTC_FOLDDISPLAYTEXT_BOXED);
  ctrl->SetIdleStyling(wxSTC_IDLESTYLING_TOVISIBLE);

  // Find the default style
  for (int i = 0; i < wxSTC_STYLE_MAX; ++i) {
    ctrl->StyleSetBackground(i, theme.bg);
    ctrl->StyleSetForeground(i, theme.fg);
    ctrl->StyleSetFont(i, theme.font);
  }

  // Line numbers in margin 0, followed by a one-pixel separator line (margin 1)
  // between them and the text; margin 2 is unused.
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  ctrl->SetMarginType(0, wxSTC_MARGIN_NUMBER);
  ctrl->SetMarginType(1, wxSTC_MARGIN_COLOUR);
  ctrl->SetMarginBackground(1, theme.brightBlack);
  ctrl->SetMarginWidth(1, 1);
  ctrl->SetMarginWidth(2, 0);
  ctrl->SetMarginLeft(4);
  ctrl->SetMarginRight(4); // Breathing room between the line and the text.
  UpdateLineNumberMargin(ctrl);

  // The line of the caret: the background a little towards the text colour, so
  // it shows on a dark or a light theme and keeps the text readable.
  ctrl->SetCaretLineBackground(Mix(theme.bg, theme.fg, 0.12));
  ctrl->SetCaretLineVisible(true);
  ctrl->SetCaretLineVisibleAlways(true);
  ctrl->SetCaretWidth(ctrl->FromDIP(2));
  // The caret is not drawn with the colour of the text: set it with each theme.
  ctrl->SetCaretForeground(theme.fg);

  // Indentation
  ctrl->SetUseTabs(false);
  ctrl->SetTabWidth(2);
  ctrl->SetIndent(2);
  ApplyOptions(ctrl);
  ctrl->FoldDisplayTextSetStyle(wxSTC_FOLDDISPLAYTEXT_BOXED);

#ifdef __WXMSW__
  ctrl->SetBufferedDraw(true);
  ctrl->SetTechnology(wxSTC_TECHNOLOGY_DIRECTWRITE);
#endif

  // The matched braces: only the text colour changes, the background stays
  // (also for the highlighted indent guide, which uses this style). Bright
  // yellow on a dark theme, cyan on a light one.
  ctrl->StyleSetBold(wxSTC_STYLE_BRACELIGHT, true);
  ctrl->StyleSetBackground(wxSTC_STYLE_BRACELIGHT, theme.bg);
  ctrl->StyleSetForeground(wxSTC_STYLE_BRACELIGHT, IsDarkColour(theme.bg)
                                                       ? theme.brightYellow
                                                       : theme.cyan);
  // A brace without a pair: red text.
  ctrl->StyleSetBold(wxSTC_STYLE_BRACEBAD, true);
  ctrl->StyleSetBackground(wxSTC_STYLE_BRACEBAD, theme.bg);
  ctrl->StyleSetForeground(wxSTC_STYLE_BRACEBAD, theme.red);
  ctrl->SetLayoutCache(wxSTC_CACHE_PAGE);
  ctrl->SetWrapMode(prefs.wrapLines ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
  ctrl->SetMultipleSelection(true);
  ctrl->SetMultiPaste(true);
  // selection
  ctrl->CmdKeyAssign(wxSTC_KEY_LEFT, wxSTC_KEYMOD_CTRL | wxSTC_KEYMOD_SHIFT,
                     wxSTC_CMD_WORDPARTLEFTEXTEND);
  ctrl->CmdKeyAssign(wxSTC_KEY_RIGHT, wxSTC_KEYMOD_CTRL | wxSTC_KEYMOD_SHIFT,
                     wxSTC_CMD_WORDPARTRIGHTEXTEND);

  // movement
  ctrl->CmdKeyAssign(wxSTC_KEY_LEFT, wxSTC_KEYMOD_CTRL, wxSTC_CMD_WORDPARTLEFT);
  ctrl->CmdKeyAssign(wxSTC_KEY_RIGHT, wxSTC_KEYMOD_CTRL,
                     wxSTC_CMD_WORDPARTRIGHT);
  // Ctrl+L deletes the line (Scintilla cuts it), like the Delete Line item.
  ctrl->CmdKeyAssign('L', wxSTC_KEYMOD_CTRL, wxSTC_CMD_LINEDELETE);

#ifdef __WXMAC__
  ctrl->CmdKeyAssign(wxSTC_KEY_DOWN, wxSTC_KEYMOD_CTRL, wxSTC_CMD_DOCUMENTEND);
  ctrl->CmdKeyAssign(wxSTC_KEY_UP, wxSTC_KEYMOD_CTRL, wxSTC_CMD_DOCUMENTSTART);

  // OSX: wxSTC_KEYMOD_CTRL => CMD key
  ctrl->CmdKeyAssign(wxSTC_KEY_RIGHT, wxSTC_KEYMOD_CTRL, wxSTC_CMD_LINEEND);
  ctrl->CmdKeyAssign(wxSTC_KEY_LEFT, wxSTC_KEYMOD_CTRL, wxSTC_CMD_HOME);

  // OSX: wxSTC_KEYMOD_META => CONTROL key
  ctrl->CmdKeyAssign(wxSTC_KEY_LEFT, wxSTC_KEYMOD_META, wxSTC_CMD_WORDPARTLEFT);
  ctrl->CmdKeyAssign(wxSTC_KEY_RIGHT, wxSTC_KEYMOD_META,
                     wxSTC_CMD_WORDPARTRIGHT);
#endif
  switch (lang) {
  case EditorLang::kText:
    InitTextStyle(ctrl, theme);
    break;
  case EditorLang::kCxx:
    InitCxxStyle(ctrl, theme);
    break;
  case EditorLang::kJava:
    InitJavaStyle(ctrl, theme);
    break;
  case EditorLang::kCMake:
    InitCMakeStyle(ctrl, theme);
    break;
  case EditorLang::kBash:
    InitBashStyle(ctrl, theme);
    break;
  case EditorLang::kMarkdown:
    InitMarkdownStyle(ctrl, theme);
    break;
  case EditorLang::kXml:
    InitXmlStyle(ctrl, theme);
    break;
  case EditorLang::kRuby:
    InitRubyStyle(ctrl, theme);
    break;
  case EditorLang::kTypeScript:
    InitTypeScriptStyle(ctrl, theme);
    break;
  case EditorLang::kJavaScript:
    InitJavaScriptStyle(ctrl, theme);
    break;
  case EditorLang::kPython:
    InitPythonStyle(ctrl, theme);
    break;
  case EditorLang::kMakefile:
    InitMakefileStyle(ctrl, theme);
    break;
  case EditorLang::kRust:
    InitRustStyle(ctrl, theme);
    break;
  case EditorLang::kDiff:
    InitDiffStyle(ctrl, theme);
    break;
  case EditorLang::kJson:
    InitJsonStyle(ctrl, theme);
    break;
  }
  // Only the guide of the caret's indent is visible: it is the highlight guide
  // (see OnUpdateUI()), so the other guides take the colour of the background.
  // The language styles set them too, so this comes after them.
  ctrl->StyleSetForeground(wxSTC_STYLE_INDENTGUIDE, theme.bg);
  ctrl->SetModEventMask(wxSTC_MOD_DELETETEXT | wxSTC_MOD_INSERTTEXT |
                        wxSTC_PERFORMED_UNDO | wxSTC_PERFORMED_REDO |
                        wxSTC_MOD_BEFOREDELETE | wxSTC_MOD_CHANGESTYLE);
}

void Editor::InitTextStyle(wxStyledTextCtrl *ctrl,
                           const wxTerminalTheme &theme) {
  // Plain text: no syntax highlighting, just the default colours.
  ctrl->SetLexer(wxSTC_LEX_NULL);
  AddProperty(ctrl, wxSTC_STYLE_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitCxxStyle(wxStyledTextCtrl *ctrl,
                          const wxTerminalTheme &theme) {
  InitCppLikeStyle(
      ctrl, theme,
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
}

void Editor::InitJavaStyle(wxStyledTextCtrl *ctrl,
                           const wxTerminalTheme &theme) {
  InitCppLikeStyle(
      ctrl, theme,
      "abstract assert boolean break byte case catch char class const "
      "continue default do double else enum extends final finally float for "
      "goto if implements import instanceof int interface long native new "
      "package permits private protected public record return sealed short "
      "static strictfp super switch synchronized this throw throws transient "
      "try var void volatile while yield true false null");
}

namespace {
const char *kJavaScriptKeywords =
    "async await break case catch class const continue debugger default "
    "delete do else export extends false finally for function if import in "
    "instanceof let new null of return static super switch this throw true "
    "try typeof undefined var void while with yield";
} // namespace

void Editor::InitJavaScriptStyle(wxStyledTextCtrl *ctrl,
                                 const wxTerminalTheme &theme) {
  InitCppLikeStyle(ctrl, theme, kJavaScriptKeywords);
}

void Editor::InitTypeScriptStyle(wxStyledTextCtrl *ctrl,
                                 const wxTerminalTheme &theme) {
  // TypeScript is JavaScript plus these.
  static const std::string keywords =
      std::string(kJavaScriptKeywords) +
      " abstract any as asserts bigint boolean constructor declare enum "
      "implements infer interface is keyof module namespace never number "
      "object override private protected public readonly require satisfies "
      "string symbol type unique unknown";
  InitCppLikeStyle(ctrl, theme, keywords.c_str());
}

void Editor::InitCppLikeStyle(wxStyledTextCtrl *ctrl,
                              const wxTerminalTheme &theme,
                              const char *keywords) {
  ctrl->SetLexer(wxSTC_LEX_CPP);

  // Primary keywords.
  ctrl->SetKeyWords(0, keywords);

  AddProperty(ctrl, wxSTC_C_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_C_COMMENT, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_C_COMMENTLINE, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_C_COMMENTDOC, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_C_COMMENTLINEDOC, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_C_COMMENTDOCKEYWORD, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_C_COMMENTDOCKEYWORDERROR, theme.bg, theme.red);
  AddProperty(ctrl, wxSTC_C_NUMBER, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_C_WORD, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_C_WORD2, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_C_STRING, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_C_STRINGEOL, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_C_STRINGRAW, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_C_CHARACTER, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_C_HASHQUOTEDSTRING, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_C_VERBATIM, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_C_ESCAPESEQUENCE, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_C_PREPROCESSOR, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_C_PREPROCESSORCOMMENT, theme.bg,
              CommentColour(theme));
  AddProperty(ctrl, wxSTC_C_PREPROCESSORCOMMENTDOC, theme.bg,
              CommentColour(theme));
  AddProperty(ctrl, wxSTC_C_OPERATOR, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_C_IDENTIFIER, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_C_UUID, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_C_REGEX, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_C_USERLITERAL, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_C_TASKMARKER, theme.bg, theme.brightYellow);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitJsonStyle(wxStyledTextCtrl *ctrl,
                           const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_JSON);

  // JSON literal keywords.
  ctrl->SetKeyWords(0, "true false null");

  AddProperty(ctrl, wxSTC_JSON_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_JSON_NUMBER, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_JSON_STRING, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_JSON_STRINGEOL, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_JSON_PROPERTYNAME, theme.bg, theme.brightBlue);
  AddProperty(ctrl, wxSTC_JSON_ESCAPESEQUENCE, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_JSON_LINECOMMENT, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_JSON_BLOCKCOMMENT, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_JSON_OPERATOR, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_JSON_URI, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_JSON_COMPACTIRI, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_JSON_KEYWORD, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_JSON_LDKEYWORD, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_JSON_ERROR, theme.bg, theme.red);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitCMakeStyle(wxStyledTextCtrl *ctrl,
                            const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_CMAKE);

  // 0: commands, 1: parameters, 2: user-defined commands.
  ctrl->SetKeyWords(
      0, "add_compile_definitions add_compile_options add_custom_command "
         "add_custom_target add_definitions add_dependencies add_executable "
         "add_library add_link_options add_subdirectory add_test "
         "cmake_minimum_required cmake_parse_arguments cmake_policy "
         "configure_file define_property enable_language enable_testing "
         "execute_process export file find_file find_library find_package "
         "find_path find_program function endfunction get_cmake_property "
         "get_directory_property get_filename_component get_property "
         "get_target_property include include_directories include_guard "
         "install link_directories link_libraries list macro endmacro "
         "mark_as_advanced math message option project return "
         "separate_arguments set set_directory_properties set_property "
         "set_target_properties set_tests_properties source_group string "
         "target_compile_definitions target_compile_features "
         "target_compile_options target_include_directories "
         "target_link_directories target_link_libraries target_link_options "
         "target_precompile_headers target_sources try_compile try_run unset");
  ctrl->SetKeyWords(
      1, "PUBLIC PRIVATE INTERFACE REQUIRED COMPONENTS OPTIONAL QUIET EXACT "
         "STATIC SHARED MODULE OBJECT ALIAS IMPORTED GLOBAL FORCE CACHE "
         "STRING BOOL PATH FILEPATH INTERNAL ON OFF TRUE FALSE YES NO NOT AND "
         "OR STREQUAL EQUAL LESS GREATER MATCHES DEFINED EXISTS COMMAND "
         "TARGET POLICY IN_LIST VERSION_LESS VERSION_GREATER VERSION_EQUAL "
         "APPEND PREPEND REMOVE_ITEM REMOVE_DUPLICATES GLOB GLOB_RECURSE "
         "REPLACE REGEX SUBSTRING TOLOWER TOUPPER LENGTH FIND PROPERTY "
         "PROPERTIES DESTINATION TARGETS FILES DIRECTORY RUNTIME LIBRARY "
         "ARCHIVE WORKING_DIRECTORY DEPENDS COMMENT VERBATIM");

  AddProperty(ctrl, wxSTC_CMAKE_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_CMAKE_COMMENT, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_CMAKE_STRINGDQ, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_CMAKE_STRINGLQ, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_CMAKE_STRINGRQ, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_CMAKE_COMMANDS, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_CMAKE_PARAMETERS, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_CMAKE_VARIABLE, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_CMAKE_USERDEFINED, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_CMAKE_WHILEDEF, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_CMAKE_FOREACHDEF, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_CMAKE_IFDEFINEDEF, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_CMAKE_MACRODEF, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_CMAKE_STRINGVAR, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_CMAKE_NUMBER, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitBashStyle(wxStyledTextCtrl *ctrl,
                           const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_BASH);

  ctrl->SetKeyWords(
      0, "if then elif else fi for while until do done case esac in function "
         "select time return exit break continue local export readonly "
         "declare typeset unset shift source alias unalias echo printf read "
         "cd pwd test eval exec set trap wait kill true false let getopts "
         "umask ulimit type command builtin");

  AddProperty(ctrl, wxSTC_SH_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_SH_ERROR, theme.bg, theme.red);
  AddProperty(ctrl, wxSTC_SH_COMMENTLINE, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_SH_NUMBER, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_SH_WORD, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_SH_STRING, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_SH_CHARACTER, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_SH_OPERATOR, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_SH_IDENTIFIER, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_SH_SCALAR, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_SH_PARAM, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_SH_BACKTICKS, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_SH_HERE_DELIM, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_SH_HERE_Q, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitMarkdownStyle(wxStyledTextCtrl *ctrl,
                               const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_MARKDOWN);

  AddProperty(ctrl, wxSTC_MARKDOWN_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_MARKDOWN_LINE_BEGIN, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_MARKDOWN_PRECHAR, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_MARKDOWN_STRONG1, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_MARKDOWN_STRONG2, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_MARKDOWN_EM1, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_MARKDOWN_EM2, theme.bg, theme.fg);
  for (int style : {wxSTC_MARKDOWN_HEADER1, wxSTC_MARKDOWN_HEADER2,
                    wxSTC_MARKDOWN_HEADER3, wxSTC_MARKDOWN_HEADER4,
                    wxSTC_MARKDOWN_HEADER5, wxSTC_MARKDOWN_HEADER6}) {
    AddProperty(ctrl, style, theme.bg, theme.brightBlue);
    ctrl->StyleSetBold(style, true);
  }
  ctrl->StyleSetBold(wxSTC_MARKDOWN_STRONG1, true);
  ctrl->StyleSetBold(wxSTC_MARKDOWN_STRONG2, true);
  ctrl->StyleSetItalic(wxSTC_MARKDOWN_EM1, true);
  ctrl->StyleSetItalic(wxSTC_MARKDOWN_EM2, true);
  AddProperty(ctrl, wxSTC_MARKDOWN_ULIST_ITEM, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_MARKDOWN_OLIST_ITEM, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_MARKDOWN_BLOCKQUOTE, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_MARKDOWN_STRIKEOUT, theme.bg, theme.brightBlack);
  AddProperty(ctrl, wxSTC_MARKDOWN_HRULE, theme.bg, theme.brightBlack);
  AddProperty(ctrl, wxSTC_MARKDOWN_LINK, theme.bg, theme.blue);
  ctrl->StyleSetUnderline(wxSTC_MARKDOWN_LINK, true);
  AddProperty(ctrl, wxSTC_MARKDOWN_CODE, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_MARKDOWN_CODE2, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_MARKDOWN_CODEBK, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitXmlStyle(wxStyledTextCtrl *ctrl,
                          const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_XML);

  AddProperty(ctrl, wxSTC_H_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_H_TAG, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_H_TAGEND, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_H_TAGUNKNOWN, theme.bg, theme.red);
  AddProperty(ctrl, wxSTC_H_ATTRIBUTE, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_H_ATTRIBUTEUNKNOWN, theme.bg, theme.red);
  AddProperty(ctrl, wxSTC_H_NUMBER, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_H_DOUBLESTRING, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_H_SINGLESTRING, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_H_VALUE, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_H_OTHER, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_H_COMMENT, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_H_ENTITY, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_H_QUESTION, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_H_XMLSTART, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_H_XMLEND, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_H_CDATA, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitRubyStyle(wxStyledTextCtrl *ctrl,
                           const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_RUBY);

  ctrl->SetKeyWords(
      0, "BEGIN END __FILE__ __LINE__ alias and attr_accessor attr_reader "
         "attr_writer begin break case class def defined? do else elsif end "
         "ensure extend false for if in include lambda module next nil not or "
         "private protected proc public raise redo require require_relative "
         "rescue retry return self super then true undef unless until when "
         "while yield");

  AddProperty(ctrl, wxSTC_RB_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_RB_ERROR, theme.bg, theme.red);
  AddProperty(ctrl, wxSTC_RB_COMMENTLINE, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_RB_POD, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_RB_DATASECTION, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_RB_NUMBER, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_RB_WORD, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_RB_WORD_DEMOTED, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_RB_CLASSNAME, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_RB_MODULE_NAME, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_RB_DEFNAME, theme.bg, theme.brightBlue);
  AddProperty(ctrl, wxSTC_RB_OPERATOR, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_RB_IDENTIFIER, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_RB_GLOBAL, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_RB_SYMBOL, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_RB_INSTANCE_VAR, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_RB_CLASS_VAR, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_RB_STDIN, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_RB_STDOUT, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_RB_STDERR, theme.bg, theme.cyan);
  for (int style : {wxSTC_RB_STRING, wxSTC_RB_CHARACTER, wxSTC_RB_REGEX,
                    wxSTC_RB_BACKTICKS, wxSTC_RB_HERE_DELIM, wxSTC_RB_HERE_Q,
                    wxSTC_RB_HERE_QQ, wxSTC_RB_HERE_QX, wxSTC_RB_STRING_Q,
                    wxSTC_RB_STRING_QQ, wxSTC_RB_STRING_QX, wxSTC_RB_STRING_QR,
                    wxSTC_RB_STRING_QW, wxSTC_RB_STRING_W, wxSTC_RB_STRING_I,
                    wxSTC_RB_STRING_QI, wxSTC_RB_STRING_QS}) {
    AddProperty(ctrl, style, theme.bg, theme.yellow);
  }
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitPythonStyle(wxStyledTextCtrl *ctrl,
                             const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_PYTHON);

  // 4 spaces is the Python convention (PEP 8).
  ctrl->SetTabWidth(4);
  ctrl->SetIndent(4);

  // 0: keywords, 1: builtins / highlighted identifiers.
  ctrl->SetKeyWords(
      0, "False None True and as assert async await break class continue def "
         "del elif else except finally for from global if import in is "
         "lambda nonlocal not or pass raise return try while with yield "
         "match case");
  ctrl->SetKeyWords(
      1, "abs all any bool bytes callable chr dict dir enumerate filter float "
         "format frozenset getattr hasattr hash id input int isinstance "
         "issubclass iter len list map max min next object open ord print "
         "range repr reversed round set setattr sorted str sum super tuple "
         "type vars zip self cls Exception ValueError TypeError KeyError");

  AddProperty(ctrl, wxSTC_P_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_P_COMMENTLINE, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_P_COMMENTBLOCK, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_P_NUMBER, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_P_WORD, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_P_WORD2, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_P_CLASSNAME, theme.bg, theme.brightBlue);
  AddProperty(ctrl, wxSTC_P_DEFNAME, theme.bg, theme.brightBlue);
  AddProperty(ctrl, wxSTC_P_DECORATOR, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_P_ATTRIBUTE, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_P_OPERATOR, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_P_IDENTIFIER, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_P_STRINGEOL, theme.bg, theme.red);
  for (int style : {wxSTC_P_STRING, wxSTC_P_CHARACTER, wxSTC_P_TRIPLE,
                    wxSTC_P_TRIPLEDOUBLE, wxSTC_P_FSTRING, wxSTC_P_FCHARACTER,
                    wxSTC_P_FTRIPLE, wxSTC_P_FTRIPLEDOUBLE}) {
    AddProperty(ctrl, style, theme.bg, theme.yellow);
  }
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitMakefileStyle(wxStyledTextCtrl *ctrl,
                               const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_MAKEFILE);

  // Recipe lines must start with a tab, so Tab inserts a real one here.
  ctrl->SetUseTabs(true);
  ctrl->SetTabWidth(8);
  ctrl->SetIndent(8);

  AddProperty(ctrl, wxSTC_MAKE_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_MAKE_COMMENT, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_MAKE_PREPROCESSOR, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_MAKE_IDENTIFIER, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_MAKE_OPERATOR, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_MAKE_TARGET, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_MAKE_IDEOL, theme.bg, theme.red);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::GotoLine(int line, int column) {
  const int lastLine = std::max(m_ctrl->GetLineCount() - 1, 0);
  const int row = std::clamp(line - 1, 0, lastLine);
  const int start = m_ctrl->PositionFromLine(row);
  const int end = m_ctrl->GetLineEndPosition(row);
  const int pos = std::min(start + std::max(column - 1, 0), end);
  m_ctrl->EnsureVisible(row);
  m_ctrl->GotoPos(pos);
  m_ctrl->ChooseCaretX();
  m_ctrl->VerticalCentreCaret();
}

void Editor::UpdateLineNumberMargin(wxStyledTextCtrl *ctrl) {
  // Wide enough for the largest line number, with a minimum so the margin does
  // not jitter while a small file is edited.
  const int digits = std::max<int>(
      3, static_cast<int>(std::to_string(ctrl->GetLineCount()).length()));
  ctrl->SetMarginWidth(0,
                       ctrl->TextWidth(wxSTC_STYLE_LINENUMBER,
                                       wxString(wxUniChar('9'), digits + 1)));
}

void Editor::OnModified(wxStyledTextEvent &event) {
  event.Skip();
  // Only a change in the number of lines can change the margin width.
  if (event.GetLinesAdded() != 0) {
    UpdateLineNumberMargin(m_ctrl);
  }
}

void Editor::OnUpdateUI(wxStyledTextEvent &event) {
  event.Skip();
  auto currentPos = m_ctrl->GetCurrentPos();
  if (m_lastUpdatePosition != currentPos) {
    m_lastUpdatePosition = currentPos;

    const int pos = currentPos;
    wxString text =
        wxString::Format(_("Ln %d, Col %d"), m_ctrl->LineFromPosition(pos) + 1,
                         m_ctrl->GetColumn(pos) + 1);
    const int from = m_ctrl->GetSelectionStart();
    const int to = m_ctrl->GetSelectionEnd();
    if (from != to) {
      // Characters, not bytes: the text is UTF-8.
      text << wxString::Format(_(" (%d selected)"),
                               m_ctrl->CountCharacters(from, to));
    }

    // Use "CallAfter" to avoid latency lags
    GetMainFrame()->CallAfter(&MainFrame::SetEditorStatus, text);
  }

  // Also when only the selection changed: it turns the highlight off.
  DoBraceMatching();
}

void Editor::SetWrapLines(bool wrap) {
  m_ctrl->SetWrapMode(wrap ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
}

void Editor::SetTheme(const wxTerminalTheme &theme) {
  m_theme = theme;
  InitEditor();
}

void Editor::SetEditorLanguage(EditorLang lang) {
  m_lang = lang;
  InitEditor();
}

void Editor::InitRustStyle(wxStyledTextCtrl *ctrl,
                           const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_RUST);

  // Keywords, then the primitive types, then the common std types.
  ctrl->SetKeyWords(
      0, "as async await break const continue crate dyn else enum extern "
         "false fn for if impl in let loop match mod move mut pub ref return "
         "self Self static struct super trait true type union unsafe use "
         "where while abstract become box do final macro override priv "
         "try typeof unsized virtual yield");
  ctrl->SetKeyWords(
      1, "bool char str i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize "
         "f32 f64");
  ctrl->SetKeyWords(
      2, "Option Some None Result Ok Err Vec String Box Rc Arc RefCell Cell "
         "HashMap HashSet BTreeMap BTreeSet VecDeque Cow Mutex RwLock");

  AddProperty(ctrl, wxSTC_RUST_DEFAULT, theme.bg, theme.fg);
  for (int style : {wxSTC_RUST_COMMENTBLOCK, wxSTC_RUST_COMMENTLINE,
                    wxSTC_RUST_COMMENTBLOCKDOC, wxSTC_RUST_COMMENTLINEDOC}) {
    AddProperty(ctrl, style, theme.bg, CommentColour(theme));
  }
  AddProperty(ctrl, wxSTC_RUST_NUMBER, theme.bg, theme.green);
  AddProperty(ctrl, wxSTC_RUST_WORD, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_RUST_WORD2, theme.bg, theme.blue);
  AddProperty(ctrl, wxSTC_RUST_WORD3, theme.bg, theme.blue);
  for (int style :
       {wxSTC_RUST_STRING, wxSTC_RUST_STRINGR, wxSTC_RUST_CHARACTER,
        wxSTC_RUST_BYTESTRING, wxSTC_RUST_BYTESTRINGR, wxSTC_RUST_BYTECHARACTER,
        wxSTC_RUST_CSTRING, wxSTC_RUST_CSTRINGR}) {
    AddProperty(ctrl, style, theme.bg, theme.yellow);
  }
  AddProperty(ctrl, wxSTC_RUST_OPERATOR, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_RUST_IDENTIFIER, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_RUST_LIFETIME, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_RUST_MACRO, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_RUST_LEXERROR, theme.bg, theme.red);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::InitDiffStyle(wxStyledTextCtrl *ctrl,
                           const wxTerminalTheme &theme) {
  ctrl->SetLexer(wxSTC_LEX_DIFF);

  // A line that was added or deleted has a light background that covers the
  // whole line (up to the right edge), and a text of the same colour. The
  // background is the colour of the theme mixed into its background, so that
  // it fits a dark and a light theme.
  const wxColour addedBg = Mix(theme.bg, theme.green, 0.22);
  const wxColour deletedBg = Mix(theme.bg, theme.red, 0.22);
  const wxColour addedFg = EnsureContrast(theme.green, addedBg, 4.0);
  const wxColour deletedFg = EnsureContrast(theme.red, deletedBg, 4.0);
  // The lexer looks at the first two characters of a line, for a diff of a
  // diff: "++", "+-", "-+" and "--" have styles of their own. Here the first
  // character is the mark of git, and the second one is the text of the line
  // ("+- [x] item" is an added line that starts with "-"). So the first
  // character decides the colour.
  for (int style :
       {wxSTC_DIFF_ADDED, wxSTC_DIFF_PATCH_ADD, wxSTC_DIFF_PATCH_DELETE}) {
    AddProperty(ctrl, style, addedBg, addedFg);
    ctrl->StyleSetEOLFilled(style, true);
  }
  for (int style : {wxSTC_DIFF_DELETED, wxSTC_DIFF_REMOVED_PATCH_ADD,
                    wxSTC_DIFF_REMOVED_PATCH_DELETE}) {
    AddProperty(ctrl, style, deletedBg, deletedFg);
    ctrl->StyleSetEOLFilled(style, true);
  }

  AddProperty(ctrl, wxSTC_DIFF_DEFAULT, theme.bg, theme.fg);
  AddProperty(ctrl, wxSTC_DIFF_COMMENT, theme.bg, CommentColour(theme));
  AddProperty(ctrl, wxSTC_DIFF_COMMAND, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_DIFF_HEADER, theme.bg, theme.cyan);
  AddProperty(ctrl, wxSTC_DIFF_POSITION, theme.bg, theme.magenta);
  AddProperty(ctrl, wxSTC_DIFF_CHANGED, theme.bg, theme.yellow);
  AddProperty(ctrl, wxSTC_STYLE_LINENUMBER, theme.bg, LineNumberColour(theme));
  AddProperty(ctrl, wxSTC_STYLE_INDENTGUIDE, theme.bg, theme.black);
}

void Editor::ApplyOptions(wxStyledTextCtrl *ctrl) {
  // The guides are drawn only when the option is on. All of them have the
  // colour of the background, but the highlighted one (see DoBraceMatching()).
  ctrl->SetIndentationGuides(AppManager::Get().GetPrefs().highlightIndentLines
                                 ? wxSTC_IV_LOOKBOTH
                                 : wxSTC_IV_NONE);
}

void Editor::OnOptionsChanged(wxCommandEvent &event) {
  event.Skip();
  ApplyOptions(m_ctrl);
  // Show the highlights again, or take them off.
  ClearBraceHighlight();
  DoBraceMatching();
}

void Editor::ClearBraceHighlight() {
  if (m_hasBraceHighlight) {
    m_hasBraceHighlight = false;
    m_ctrl->BraceHighlight(wxSTC_INVALID_POSITION, wxSTC_INVALID_POSITION);
    m_ctrl->SetHighlightGuide(0); // The indent guide that was highlighted
  }
}

void Editor::DoBraceMatching() {
  // No highlight when the option is off, or while text is selected.
  if (!AppManager::Get().GetPrefs().highlightBraces ||
      m_ctrl->GetSelectionStart() != m_ctrl->GetSelectionEnd()) {
    ClearBraceHighlight();
    return;
  }

  // The brace at the caret, or else the one before it.
  const auto isBrace = [](int ch) {
    return ch > 0 && std::strchr("<>{}()[]", ch) != nullptr;
  };
  int pos = m_ctrl->GetCurrentPos();
  if (!isBrace(m_ctrl->GetCharAt(pos))) {
    if (pos == 0) {
      ClearBraceHighlight();
      return;
    }
    pos = m_ctrl->PositionBefore(pos);
    if (!isBrace(m_ctrl->GetCharAt(pos))) {
      ClearBraceHighlight();
      return;
    }
  }

  m_hasBraceHighlight = true; // A good or a bad highlight
  int indentCol = 0;
  const int endPos = m_ctrl->BraceMatch(pos);
  if (endPos != wxSTC_INVALID_POSITION) {
    m_ctrl->BraceHighlight(pos, endPos);
    if (m_ctrl->GetIndentationGuides() != 0 && m_ctrl->GetIndent() > 0) {
      // The indent guide of the block, rounded down to a guide column.
      indentCol = std::min(
          m_ctrl->GetLineIndentation(m_ctrl->LineFromPosition(pos)),
          m_ctrl->GetLineIndentation(m_ctrl->LineFromPosition(endPos)));
      indentCol = indentCol / m_ctrl->GetIndent() * m_ctrl->GetIndent();
    }
  } else {
    m_ctrl->BraceBadLight(pos);
  }
  m_ctrl->SetHighlightGuide(indentCol);
}
