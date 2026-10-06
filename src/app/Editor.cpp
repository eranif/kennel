#include "app/Editor.h"
#include "wx/sizer.h"

#include <algorithm>
#include <string>

Editor::Editor(wxWindow *parent, EditorLang lang, const wxTerminalTheme &theme)
    : wxPanel(parent), m_lang{lang}, m_theme{theme} {
  SetSizer(new wxBoxSizer(wxVERTICAL));
  m_ctrl = new wxStyledTextCtrl(this);
  GetSizer()->Add(m_ctrl, wxSizerFlags(1).Expand());
  GetSizer()->Fit(this);
  Layout();
  m_ctrl->Bind(wxEVT_STC_MODIFIED, &Editor::OnModified, this);
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

  // Line numbers in margin 0, followed by a one-pixel separator line (margin 1)
  // between them and the text; margin 2 is unused.
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  m_ctrl->SetMarginType(0, wxSTC_MARGIN_NUMBER);
  m_ctrl->SetMarginType(1, wxSTC_MARGIN_COLOUR);
  m_ctrl->SetMarginBackground(1, m_theme.brightBlack);
  m_ctrl->SetMarginWidth(1, 1);
  m_ctrl->SetMarginWidth(2, 0);
  m_ctrl->SetMarginLeft(4);
  m_ctrl->SetMarginRight(4); // Breathing room between the line and the text.
  UpdateLineNumberMargin();

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
  case EditorLang::kJava:
    InitJavaStyle();
    break;
  case EditorLang::kCMake:
    InitCMakeStyle();
    break;
  case EditorLang::kBash:
    InitBashStyle();
    break;
  case EditorLang::kMarkdown:
    InitMarkdownStyle();
    break;
  case EditorLang::kXml:
    InitXmlStyle();
    break;
  case EditorLang::kRuby:
    InitRubyStyle();
    break;
  case EditorLang::kTypeScript:
    InitTypeScriptStyle();
    break;
  case EditorLang::kJavaScript:
    InitJavaScriptStyle();
    break;
  case EditorLang::kPython:
    InitPythonStyle();
    break;
  case EditorLang::kMakefile:
    InitMakefileStyle();
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
  InitCppLikeStyle(
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

void Editor::InitJavaStyle() {
  InitCppLikeStyle(
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

void Editor::InitJavaScriptStyle() { InitCppLikeStyle(kJavaScriptKeywords); }

void Editor::InitTypeScriptStyle() {
  // TypeScript is JavaScript plus these.
  static const std::string keywords =
      std::string(kJavaScriptKeywords) +
      " abstract any as asserts bigint boolean constructor declare enum "
      "implements infer interface is keyof module namespace never number "
      "object override private protected public readonly require satisfies "
      "string symbol type unique unknown";
  InitCppLikeStyle(keywords.c_str());
}

void Editor::InitCppLikeStyle(const char *keywords) {
  m_ctrl->SetLexer(wxSTC_LEX_CPP);

  // Primary keywords.
  m_ctrl->SetKeyWords(0, keywords);

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

void Editor::InitCMakeStyle() {
  m_ctrl->SetLexer(wxSTC_LEX_CMAKE);

  // 0: commands, 1: parameters, 2: user-defined commands.
  m_ctrl->SetKeyWords(
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
  m_ctrl->SetKeyWords(
      1, "PUBLIC PRIVATE INTERFACE REQUIRED COMPONENTS OPTIONAL QUIET EXACT "
         "STATIC SHARED MODULE OBJECT ALIAS IMPORTED GLOBAL FORCE CACHE "
         "STRING BOOL PATH FILEPATH INTERNAL ON OFF TRUE FALSE YES NO NOT AND "
         "OR STREQUAL EQUAL LESS GREATER MATCHES DEFINED EXISTS COMMAND "
         "TARGET POLICY IN_LIST VERSION_LESS VERSION_GREATER VERSION_EQUAL "
         "APPEND PREPEND REMOVE_ITEM REMOVE_DUPLICATES GLOB GLOB_RECURSE "
         "REPLACE REGEX SUBSTRING TOLOWER TOUPPER LENGTH FIND PROPERTY "
         "PROPERTIES DESTINATION TARGETS FILES DIRECTORY RUNTIME LIBRARY "
         "ARCHIVE WORKING_DIRECTORY DEPENDS COMMENT VERBATIM");

  AddProperty(wxSTC_CMAKE_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_CMAKE_COMMENT, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_CMAKE_STRINGDQ, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_CMAKE_STRINGLQ, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_CMAKE_STRINGRQ, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_CMAKE_COMMANDS, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_CMAKE_PARAMETERS, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_CMAKE_VARIABLE, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_CMAKE_USERDEFINED, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_CMAKE_WHILEDEF, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_CMAKE_FOREACHDEF, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_CMAKE_IFDEFINEDEF, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_CMAKE_MACRODEF, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_CMAKE_STRINGVAR, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_CMAKE_NUMBER, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::InitBashStyle() {
  m_ctrl->SetLexer(wxSTC_LEX_BASH);

  m_ctrl->SetKeyWords(
      0, "if then elif else fi for while until do done case esac in function "
         "select time return exit break continue local export readonly "
         "declare typeset unset shift source alias unalias echo printf read "
         "cd pwd test eval exec set trap wait kill true false let getopts "
         "umask ulimit type command builtin");

  AddProperty(wxSTC_SH_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_SH_ERROR, m_theme.bg, m_theme.red);
  AddProperty(wxSTC_SH_COMMENTLINE, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_SH_NUMBER, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_SH_WORD, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_SH_STRING, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_SH_CHARACTER, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_SH_OPERATOR, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_SH_IDENTIFIER, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_SH_SCALAR, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_SH_PARAM, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_SH_BACKTICKS, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_SH_HERE_DELIM, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_SH_HERE_Q, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::InitMarkdownStyle() {
  m_ctrl->SetLexer(wxSTC_LEX_MARKDOWN);

  AddProperty(wxSTC_MARKDOWN_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_MARKDOWN_LINE_BEGIN, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_MARKDOWN_PRECHAR, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_MARKDOWN_STRONG1, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_MARKDOWN_STRONG2, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_MARKDOWN_EM1, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_MARKDOWN_EM2, m_theme.bg, m_theme.fg);
  for (int style : {wxSTC_MARKDOWN_HEADER1, wxSTC_MARKDOWN_HEADER2,
                    wxSTC_MARKDOWN_HEADER3, wxSTC_MARKDOWN_HEADER4,
                    wxSTC_MARKDOWN_HEADER5, wxSTC_MARKDOWN_HEADER6}) {
    AddProperty(style, m_theme.bg, m_theme.brightBlue);
    m_ctrl->StyleSetBold(style, true);
  }
  m_ctrl->StyleSetBold(wxSTC_MARKDOWN_STRONG1, true);
  m_ctrl->StyleSetBold(wxSTC_MARKDOWN_STRONG2, true);
  m_ctrl->StyleSetItalic(wxSTC_MARKDOWN_EM1, true);
  m_ctrl->StyleSetItalic(wxSTC_MARKDOWN_EM2, true);
  AddProperty(wxSTC_MARKDOWN_ULIST_ITEM, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_MARKDOWN_OLIST_ITEM, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_MARKDOWN_BLOCKQUOTE, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_MARKDOWN_STRIKEOUT, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_MARKDOWN_HRULE, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_MARKDOWN_LINK, m_theme.bg, m_theme.blue);
  m_ctrl->StyleSetUnderline(wxSTC_MARKDOWN_LINK, true);
  AddProperty(wxSTC_MARKDOWN_CODE, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_MARKDOWN_CODE2, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_MARKDOWN_CODEBK, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::InitXmlStyle() {
  m_ctrl->SetLexer(wxSTC_LEX_XML);

  AddProperty(wxSTC_H_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_H_TAG, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_H_TAGEND, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_H_TAGUNKNOWN, m_theme.bg, m_theme.red);
  AddProperty(wxSTC_H_ATTRIBUTE, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_H_ATTRIBUTEUNKNOWN, m_theme.bg, m_theme.red);
  AddProperty(wxSTC_H_NUMBER, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_H_DOUBLESTRING, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_H_SINGLESTRING, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_H_VALUE, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_H_OTHER, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_H_COMMENT, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_H_ENTITY, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_H_QUESTION, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_H_XMLSTART, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_H_XMLEND, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_H_CDATA, m_theme.bg, m_theme.yellow);
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::InitRubyStyle() {
  m_ctrl->SetLexer(wxSTC_LEX_RUBY);

  m_ctrl->SetKeyWords(
      0, "BEGIN END __FILE__ __LINE__ alias and attr_accessor attr_reader "
         "attr_writer begin break case class def defined? do else elsif end "
         "ensure extend false for if in include lambda module next nil not or "
         "private protected proc public raise redo require require_relative "
         "rescue retry return self super then true undef unless until when "
         "while yield");

  AddProperty(wxSTC_RB_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_RB_ERROR, m_theme.bg, m_theme.red);
  AddProperty(wxSTC_RB_COMMENTLINE, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_RB_POD, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_RB_DATASECTION, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_RB_NUMBER, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_RB_WORD, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_RB_WORD_DEMOTED, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_RB_CLASSNAME, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_RB_MODULE_NAME, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_RB_DEFNAME, m_theme.bg, m_theme.brightBlue);
  AddProperty(wxSTC_RB_OPERATOR, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_RB_IDENTIFIER, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_RB_GLOBAL, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_RB_SYMBOL, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_RB_INSTANCE_VAR, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_RB_CLASS_VAR, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_RB_STDIN, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_RB_STDOUT, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_RB_STDERR, m_theme.bg, m_theme.cyan);
  for (int style : {wxSTC_RB_STRING, wxSTC_RB_CHARACTER, wxSTC_RB_REGEX,
                    wxSTC_RB_BACKTICKS, wxSTC_RB_HERE_DELIM, wxSTC_RB_HERE_Q,
                    wxSTC_RB_HERE_QQ, wxSTC_RB_HERE_QX, wxSTC_RB_STRING_Q,
                    wxSTC_RB_STRING_QQ, wxSTC_RB_STRING_QX, wxSTC_RB_STRING_QR,
                    wxSTC_RB_STRING_QW, wxSTC_RB_STRING_W, wxSTC_RB_STRING_I,
                    wxSTC_RB_STRING_QI, wxSTC_RB_STRING_QS}) {
    AddProperty(style, m_theme.bg, m_theme.yellow);
  }
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::InitPythonStyle() {
  m_ctrl->SetLexer(wxSTC_LEX_PYTHON);

  // 4 spaces is the Python convention (PEP 8).
  m_ctrl->SetTabWidth(4);
  m_ctrl->SetIndent(4);

  // 0: keywords, 1: builtins / highlighted identifiers.
  m_ctrl->SetKeyWords(
      0, "False None True and as assert async await break class continue def "
         "del elif else except finally for from global if import in is "
         "lambda nonlocal not or pass raise return try while with yield "
         "match case");
  m_ctrl->SetKeyWords(
      1, "abs all any bool bytes callable chr dict dir enumerate filter float "
         "format frozenset getattr hasattr hash id input int isinstance "
         "issubclass iter len list map max min next object open ord print "
         "range repr reversed round set setattr sorted str sum super tuple "
         "type vars zip self cls Exception ValueError TypeError KeyError");

  AddProperty(wxSTC_P_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_P_COMMENTLINE, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_P_COMMENTBLOCK, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_P_NUMBER, m_theme.bg, m_theme.green);
  AddProperty(wxSTC_P_WORD, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_P_WORD2, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_P_CLASSNAME, m_theme.bg, m_theme.brightBlue);
  AddProperty(wxSTC_P_DEFNAME, m_theme.bg, m_theme.brightBlue);
  AddProperty(wxSTC_P_DECORATOR, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_P_ATTRIBUTE, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_P_OPERATOR, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_P_IDENTIFIER, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_P_STRINGEOL, m_theme.bg, m_theme.red);
  for (int style : {wxSTC_P_STRING, wxSTC_P_CHARACTER, wxSTC_P_TRIPLE,
                    wxSTC_P_TRIPLEDOUBLE, wxSTC_P_FSTRING, wxSTC_P_FCHARACTER,
                    wxSTC_P_FTRIPLE, wxSTC_P_FTRIPLEDOUBLE}) {
    AddProperty(style, m_theme.bg, m_theme.yellow);
  }
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::InitMakefileStyle() {
  m_ctrl->SetLexer(wxSTC_LEX_MAKEFILE);

  // Recipe lines must start with a tab, so Tab inserts a real one here.
  m_ctrl->SetUseTabs(true);
  m_ctrl->SetTabWidth(8);
  m_ctrl->SetIndent(8);

  AddProperty(wxSTC_MAKE_DEFAULT, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_MAKE_COMMENT, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_MAKE_PREPROCESSOR, m_theme.bg, m_theme.magenta);
  AddProperty(wxSTC_MAKE_IDENTIFIER, m_theme.bg, m_theme.cyan);
  AddProperty(wxSTC_MAKE_OPERATOR, m_theme.bg, m_theme.fg);
  AddProperty(wxSTC_MAKE_TARGET, m_theme.bg, m_theme.blue);
  AddProperty(wxSTC_MAKE_IDEOL, m_theme.bg, m_theme.red);
  AddProperty(wxSTC_STYLE_LINENUMBER, m_theme.bg, m_theme.brightBlack);
  AddProperty(wxSTC_STYLE_INDENTGUIDE, m_theme.bg, m_theme.black);
}

void Editor::UpdateLineNumberMargin() {
  // Wide enough for the largest line number, with a minimum so the margin does
  // not jitter while a small file is edited.
  const int digits = std::max<int>(
      3, static_cast<int>(std::to_string(m_ctrl->GetLineCount()).length()));
  m_ctrl->SetMarginWidth(
      0, m_ctrl->TextWidth(wxSTC_STYLE_LINENUMBER,
                           wxString(wxUniChar('9'), digits + 1)));
}

void Editor::OnModified(wxStyledTextEvent &event) {
  event.Skip();
  // Only a change in the number of lines can change the margin width.
  if (event.GetLinesAdded() != 0) {
    UpdateLineNumberMargin();
  }
}

void Editor::SetTheme(const wxTerminalTheme &theme) {
  m_theme = theme;
  InitEditor();
}

void Editor::SetEditorLanguage(EditorLang lang) {
  m_lang = lang;
  InitEditor();
}
