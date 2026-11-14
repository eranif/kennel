#include "FindResultsView.hpp"

#include "app/Editor.h"
#include "app/FileEvent.hpp"
#include "app/FileTreeCtrl.hpp"
#include "app/ThemeManager.h"
#include "app/ToolBarUtils.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"

#include <algorithm>
#include <wx/aui/auibar.h>
#include <wx/cursor.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stc/stc.h>

namespace {
const int kIdStop = wxWindow::NewControlId();
const int kIdClear = wxWindow::NewControlId();

// The styles of the text, besides the default one.
constexpr int kStyleFile = 1;  // The path of a file
constexpr int kStyleLine = 2;  // The line number of a match, a detail
constexpr int kStyleTitle = 3; // What was asked, how it ended
constexpr int kStyleError = 4; // A search that failed
constexpr int kStyleDone = 5;  // A search that found something and ended
constexpr int kStyleWarn = 6;  // A search that found nothing, or did not end
constexpr int kStyleMatch = 7; // The matched text in a line

// How many bytes `text` takes in the text control (it is UTF-8).
int Utf8Length(const wxString &text) {
  return static_cast<int>(text.ToUTF8().length());
}

// How many units of `text` (the indexes of wxString) the `count` characters
// that start at index `from` take. A character outside the BMP takes two on
// Windows, where a wxString is UTF-16; the search counts it as one.
size_t UnitsOf(const wxString &text, size_t from, int count) {
  size_t index = from;
  for (int i = 0; i < count && index < text.length(); ++i) {
    const wxUint32 c = text[index].GetValue();
    const bool highSurrogate =
        sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF;
    index += highSurrogate && index + 1 < text.length() ? 2 : 1;
  }
  return index - from;
}

// Text that is appended to the control, with the style of each part.
class Styled {
public:
  void Add(const wxString &text, int style) {
    const int length = Utf8Length(text);
    m_text += text;
    m_spans.push_back({style, length});
  }
  // Appends it to `ctrl`, which is read only for the user.
  void WriteTo(wxStyledTextCtrl *ctrl) const {
    const int start = ctrl->GetLength();
    ctrl->SetReadOnly(false);
    ctrl->AppendText(m_text);
    ctrl->StartStyling(start);
    for (const Span &span : m_spans) {
      ctrl->SetStyling(span.length, span.style);
    }
    ctrl->SetReadOnly(true);
  }

private:
  struct Span {
    int style;
    int length;
  };
  wxString m_text;
  std::vector<Span> m_spans;
};

// "regex, match case"; empty when no flag is on.
wxString FlagsOf(const FindQuery &query) {
  wxString flags;
  const auto add = [&](bool on, const wxString &name) {
    if (on) {
      flags += (flags.empty() ? wxString() : wxString(", ")) + name;
    }
  };
  add(query.isRegex, _("regex"));
  add(query.matchCase, _("match case"));
  add(query.wholeWord, _("whole word"));
  return flags;
}
} // namespace

FindResultsView::FindResultsView(wxWindow *parent)
    : wxPanel(parent, wxID_ANY), m_search(this) {
  const auto &bitmaps = AppManager::Get().GetBitmaps();
  m_toolbar = new wxAuiToolBar(this);
  toolbar::AddTool(m_toolbar, kIdStop, _("Stop"),
                   bitmaps.GetByAlias("stop", false),
                   _("Stop the search that runs"));
  toolbar::AddTool(m_toolbar, kIdClear, _("Clear"),
                   bitmaps.GetByAlias("clear", false), _("Clear the results"));
  m_toolbar->Realize();
  toolbar::ApplyStyle(m_toolbar);

  m_text = new wxStyledTextCtrl(this, wxID_ANY);

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_toolbar, wxSizerFlags(0).Expand());
  sizer->Add(m_text, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  ApplyTheme();
  AppendMessage(_("No search yet. Press Ctrl+Shift+F."), kStyleLine);

  Bind(wxEVT_FIND_BATCH, &FindResultsView::OnBatch, this);
  Bind(wxEVT_FIND_DONE, &FindResultsView::OnDone, this);
  m_toolbar->Bind(wxEVT_TOOL, &FindResultsView::OnStop, this, kIdStop);
  m_toolbar->Bind(wxEVT_TOOL, &FindResultsView::OnClear, this, kIdClear);
  m_toolbar->Bind(wxEVT_UPDATE_UI, &FindResultsView::OnStopUI, this, kIdStop);
  m_toolbar->Bind(wxEVT_UPDATE_UI, &FindResultsView::OnClearUI, this, kIdClear);
  // A click opens: the control does not get the mouse, so nothing is selected.
  m_text->Bind(wxEVT_LEFT_DOWN, &FindResultsView::OnLeftDown, this);
  m_text->Bind(wxEVT_LEFT_DCLICK, &FindResultsView::OnIgnoreMouse, this);
  m_text->Bind(wxEVT_LEFT_UP, &FindResultsView::OnIgnoreMouse, this);
  m_text->Bind(wxEVT_SET_CURSOR, &FindResultsView::OnSetCursor, this);
  m_text->Bind(wxEVT_KEY_DOWN, &FindResultsView::OnKeyDown, this);
  EventNotifier::Get()->Bind(wxEVT_THEME_APPLIED,
                             &FindResultsView::OnThemeApplied, this);
}

FindResultsView::~FindResultsView() {
  EventNotifier::Get()->Unbind(wxEVT_THEME_APPLIED,
                               &FindResultsView::OnThemeApplied, this);
}

bool FindResultsView::IsRunning() const { return m_search.IsRunning(); }

void FindResultsView::ApplyToolBarStyle() { toolbar::ApplyStyle(m_toolbar); }

// ---------------------------------------------------------------------------
// Searching
// ---------------------------------------------------------------------------

Status FindResultsView::Search(const Account &account, const FindQuery &query) {
  if (m_search.IsRunning()) {
    // Not now: the one that runs is stopped, and this one starts when it says
    // that it is done (OnDone). A remote search can not be interrupted (see
    // FindInFiles), so this can take a while: say what is going on.
    m_pending = Pending{account, query};
    m_search.Cancel();
    AppendMessage(
        wxString::Format(
            _("Stopping the search for \"%s\" ... \"%s\" starts after it"),
            m_query ? m_query->pattern : wxString(), query.pattern),
        kStyleLine);
    return Status::Ok();
  }
  m_pending.reset();
  Status started = m_search.Start(account, query);
  if (!started.ok()) {
    return started;
  }
  Begin(account, query);
  return started;
}

void FindResultsView::Begin(const Account &account, const FindQuery &query) {
  Clear();
  m_generation = m_search.GetGeneration();
  m_account = account;
  m_query = query;
  AppendQuery(account, query);
}

void FindResultsView::Stop() {
  m_pending.reset();
  m_search.Cancel();
}

void FindResultsView::OnStop(wxCommandEvent &) { Stop(); }

void FindResultsView::OnStopUI(wxUpdateUIEvent &event) {
  event.Enable(m_search.IsRunning());
}

void FindResultsView::OnClear(wxCommandEvent &) {
  // The search that runs is stopped: its matches would come into an empty view.
  Stop();
  m_generation = 0;
  m_query.reset();
  Clear();
}

void FindResultsView::OnClearUI(wxUpdateUIEvent &event) {
  event.Enable(m_query.has_value() || m_search.IsRunning());
}

void FindResultsView::OnBatch(FindEvent &event) {
  if (event.GetInt() != m_generation) {
    return; // Of a search that was replaced or cleared: its answer came late
  }
  Append(event.GetMatches());
}

void FindResultsView::OnDone(FindEvent &event) {
  // A search is only replaced while it runs, so the answer that ends it is the
  // one of the last Start(), whatever the view shows.
  if (m_pending && event.GetInt() == m_search.GetGeneration()) {
    // The search that ended was stopped for this one.
    const Pending next = *m_pending;
    m_pending.reset();
    if (const Status started = m_search.Start(next.account, next.query);
        started.ok()) {
      Begin(next.account, next.query);
    } else {
      // The results of the search that was stopped are not those of this one.
      Clear();
      m_generation = 0;
      AppendMessage(started.message(), kStyleError);
    }
    return;
  }
  if (event.GetInt() != m_generation) {
    return;
  }

  AppendMessage(wxEmptyString, wxSTC_STYLE_DEFAULT);
  if (!event.GetResult().ok()) {
    AppendMessage(wxString::Format(_("The search failed: %s"),
                                   event.GetResult().message()),
                  kStyleError);
    if (m_matchCount > 0) {
      AppendMessage(wxString::Format(_("%zu matches before it"), m_matchCount),
                    kStyleLine);
    }
    return;
  }
  wxString summary = m_matchCount == 0
                         ? wxString(_("No matches"))
                         : wxString::Format(_("%zu matches in %zu files"),
                                            m_matchCount, m_fileCount);
  if (event.IsCancelled()) {
    summary += _(" (stopped)");
  } else if (event.IsTruncated()) {
    summary +=
        wxString::Format(_(" (only the first %zu)"), FindInFiles::kMaxMatches);
  }
  AppendMessage(summary,
                m_matchCount == 0 || event.IsCancelled() || event.IsTruncated()
                    ? kStyleWarn
                    : kStyleDone);
}

// ---------------------------------------------------------------------------
// The text
// ---------------------------------------------------------------------------

void FindResultsView::Clear() {
  m_rows.clear();
  m_matchCount = 0;
  m_fileCount = 0;
  m_lastPath.clear();
  m_text->SetReadOnly(false);
  m_text->ClearAll();
  m_text->SetReadOnly(true);
}

wxString FindResultsView::Relative(const wxString &path) const {
  if (!m_query || m_query->root.empty() || !path.StartsWith(m_query->root)) {
    return path;
  }
  const wxString &root = m_query->root;
  const wxString rest = path.Mid(root.length());
  if (root.EndsWith("/") || root.EndsWith("\\")) {
    return rest;
  }
  // "/proj2/a" is not below "/proj": the root is only a prefix of it.
  if (!rest.empty() && (rest[0] == '/' || rest[0] == '\\')) {
    return rest.Mid(1);
  }
  return path;
}

void FindResultsView::AppendMessage(const wxString &text, int style) {
  Styled out;
  out.Add(text + "\n", style);
  out.WriteTo(m_text);
  m_rows.push_back({});
}

void FindResultsView::AppendQuery(const Account &account,
                                  const FindQuery &query) {
  // One line: what is looked for, where, and what is set.
  wxString banner =
      wxString::Format(_("Searching for \"%s\" in %s: %s"), query.pattern,
                       account.GetDisplayName(), query.root);
  if (const wxString flags = FlagsOf(query); !flags.empty()) {
    banner += wxString::Format(_("  [%s]"), flags);
  }
  if (!query.include.empty()) {
    banner += wxString::Format(_("  include: %s"), query.include);
  }
  if (!query.exclude.empty()) {
    banner += wxString::Format(_("  exclude: %s"), query.exclude);
  }
  AppendMessage(banner, kStyleTitle);
}

void FindResultsView::Append(const std::vector<FindMatch> &matches) {
  if (matches.empty()) {
    return;
  }
  Styled out;
  for (const FindMatch &match : matches) {
    if (match.path != m_lastPath) {
      m_lastPath = match.path;
      ++m_fileCount;
      out.Add(Relative(match.path) + "\n", kStyleFile);
      m_rows.push_back({match.path, 0, 0, 0});
    }
    ++m_matchCount;
    out.Add(wxString::Format("  %d: ", match.line), kStyleLine);
    // The text before the match, the match, and the rest.
    const int start = std::clamp<int>(match.matchStart, 0, match.text.length());
    const int length =
        std::clamp<int>(match.matchLength, 0, match.text.length() - start);
    out.Add(match.text.Left(start), wxSTC_STYLE_DEFAULT);
    out.Add(match.text.Mid(start, length), kStyleMatch);
    out.Add(match.text.Mid(start + length) + "\n", wxSTC_STYLE_DEFAULT);
    m_rows.push_back({match.path, match.line, match.column, length});
  }
  out.WriteTo(m_text);
}

// ---------------------------------------------------------------------------
// Opening a match
// ---------------------------------------------------------------------------

void FindResultsView::OnLeftDown(wxMouseEvent &event) {
  // The control never sees the press, so it does not start a selection; the
  // focus is ours to take, for the keys.
  m_text->SetFocus();
  const int position = m_text->PositionFromPoint(event.GetPosition());
  if (position == wxSTC_INVALID_POSITION) {
    return;
  }
  const int line = m_text->LineFromPosition(position);
  m_text->GotoLine(line); // The line of the caret shows where the user is
  Activate(line);
}

void FindResultsView::OnIgnoreMouse(wxMouseEvent &) {}

void FindResultsView::OnSetCursor(wxSetCursorEvent &event) {
  event.SetCursor(wxCursor(wxCURSOR_HAND));
}

void FindResultsView::OnKeyDown(wxKeyEvent &event) {
  if (event.GetModifiers() == wxMOD_NONE &&
      (event.GetKeyCode() == WXK_RETURN ||
       event.GetKeyCode() == WXK_NUMPAD_ENTER)) {
    Activate(m_text->GetCurrentLine());
    return;
  }
  event.Skip();
}

void FindResultsView::Activate(int line) {
  if (line < 0 || static_cast<size_t>(line) >= m_rows.size()) {
    return;
  }
  const Row &row = m_rows[line];
  if (row.path.empty()) {
    return; // A message
  }
  FileEvent open{wxEVT_FILETREE_ACTIVATED};
  open.SetEventObject(this);
  open.SetPath(row.path);
  open.SetIsDir(false);
  open.SetAccount(m_account);
  open.SetLocation(row.line, row.column, row.length);
  EventNotifier::Get()->AddPendingEvent(open);
}

// ---------------------------------------------------------------------------
// The theme
// ---------------------------------------------------------------------------

void FindResultsView::OnThemeApplied(wxCommandEvent &event) {
  event.Skip();
  ApplyTheme();
}

void FindResultsView::ApplyTheme() {
  const wxTerminalTheme theme = ThemeManager::Get().ActiveTheme().value_or(
      wxSystemSettings::GetAppearance().IsDark()
          ? wxTerminalTheme::MakeDarkTheme()
          : wxTerminalTheme::MakeLightTheme());

  Editor::InitEditor(m_text, EditorLang::kText, theme);
  // The text is styled by hand (see Styled), not by a lexer.
  m_text->SetLexer(wxSTC_LEX_CONTAINER);
  // No line numbers: the lines of the file are in the text itself.
  m_text->SetMarginWidth(0, 0);
  m_text->SetMarginWidth(1, 0);
  m_text->SetWrapMode(wxSTC_WRAP_NONE);

  // Nothing is selected, and there is no caret: the line of the caret is what
  // shows the row that Enter opens.
  m_text->SetSelBackground(true, theme.bg);
  m_text->SetSelForeground(false, theme.fg);
  m_text->SetCaretStyle(wxSTC_CARETSTYLE_INVISIBLE);

  m_text->StyleSetForeground(kStyleFile, theme.linkColour);
  m_text->StyleSetBold(kStyleFile, true);
  m_text->StyleSetForeground(kStyleLine, theme.brightBlack);
  // The messages have colours of their own, for a dark theme and a light one.
  const bool dark = theme.bg.GetLuminance() < 0.5;
  m_text->StyleSetBold(kStyleTitle, true);
  m_text->StyleSetForeground(kStyleTitle, dark ? theme.brightCyan : theme.blue);
  m_text->StyleSetBold(kStyleDone, true);
  m_text->StyleSetForeground(kStyleDone,
                             dark ? theme.brightGreen : theme.green);
  m_text->StyleSetBold(kStyleWarn, true);
  m_text->StyleSetForeground(kStyleWarn,
                             dark ? theme.brightYellow : theme.magenta);
  m_text->StyleSetForeground(kStyleError, dark ? theme.brightRed : theme.red);
  // The match has its own text colour, not a background.
  m_text->StyleSetBold(kStyleMatch, true);
  m_text->StyleSetForeground(kStyleMatch,
                             dark ? theme.brightYellow : theme.magenta);

  m_text->SetReadOnly(true);
  m_text->SetBackgroundColour(theme.bg);
  Refresh();
}
