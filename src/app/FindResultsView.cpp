#include "FindResultsView.hpp"

#include "app/Editor.h"
#include "app/FileEvent.hpp"
#include "app/FileTreeCtrl.hpp"
#include "app/ThemeManager.h"
#include "core/EventNotifier.hpp"

#include <wx/button.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>

namespace {
// The styles of the text, besides the default one.
constexpr int kStyleFile = 1;
constexpr int kStyleLine = 2;
// The indicator that marks the match in a line.
constexpr int kIndicatorMatch = 0;

// How many bytes `text` takes in the text control (it is UTF-8).
int Utf8Length(const wxString &text) {
  return static_cast<int>(text.ToUTF8().length());
}
} // namespace

FindResultsView::FindResultsView(wxWindow *parent)
    : wxPanel(parent, wxID_ANY), m_search(this) {
  auto *sizer = new wxBoxSizer(wxVERTICAL);

  auto *top = new wxBoxSizer(wxHORIZONTAL);
  m_header = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_stop = new wxButton(this, wxID_ANY, _("Stop"), wxDefaultPosition,
                        wxDefaultSize, wxBU_EXACTFIT);
  top->Add(m_header, wxSizerFlags(1).Center().Border(wxALL, 4));
  top->Add(m_stop, wxSizerFlags(0).Center().Border(wxALL, 2));
  sizer->Add(top, wxSizerFlags(0).Expand());

  m_text = new wxStyledTextCtrl(this, wxID_ANY);
  sizer->Add(m_text, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  ApplyTheme();
  SetHeader(_("No search yet. Press Ctrl+Shift+F."));

  Bind(wxEVT_FIND_BATCH, &FindResultsView::OnBatch, this);
  Bind(wxEVT_FIND_DONE, &FindResultsView::OnDone, this);
  m_stop->Bind(wxEVT_BUTTON, &FindResultsView::OnStop, this);
  m_stop->Bind(wxEVT_UPDATE_UI, &FindResultsView::OnStopUI, this);
  m_text->Bind(wxEVT_STC_DOUBLECLICK, &FindResultsView::OnDoubleClick, this);
  m_text->Bind(wxEVT_KEY_DOWN, &FindResultsView::OnKeyDown, this);
  EventNotifier::Get()->Bind(wxEVT_THEME_APPLIED,
                             &FindResultsView::OnThemeApplied, this);
}

FindResultsView::~FindResultsView() {
  EventNotifier::Get()->Unbind(wxEVT_THEME_APPLIED,
                               &FindResultsView::OnThemeApplied, this);
}

bool FindResultsView::IsRunning() const { return m_search.IsRunning(); }

// ---------------------------------------------------------------------------
// Searching
// ---------------------------------------------------------------------------

Status FindResultsView::Search(const Account &account, const FindQuery &query) {
  if (m_search.IsRunning()) {
    // Not now: the one that runs is stopped, and this one starts when it says
    // that it is done (OnDone).
    m_pending = Pending{account, query};
    m_search.Cancel();
    // A remote search can not be interrupted (see FindInFiles), so this can
    // take a while: say what is going on.
    SetHeader(wxString::Format(
        _("Stopping the search for \"%s\" ... \"%s\" starts after it"),
        m_query ? m_query->pattern : wxString(), query.pattern));
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
  SetHeader(wxString::Format(_("Searching for \"%s\" in %s: %s ..."),
                             query.pattern, account.GetDisplayName(),
                             query.root));
}

void FindResultsView::Stop() {
  m_pending.reset();
  m_search.Cancel();
}

void FindResultsView::OnStop(wxCommandEvent &) { Stop(); }

void FindResultsView::OnStopUI(wxUpdateUIEvent &event) {
  event.Enable(m_search.IsRunning());
}

void FindResultsView::OnBatch(FindEvent &event) {
  if (event.GetInt() != m_generation) {
    return; // Of a search that was replaced: its answer came late
  }
  Append(event.GetMatches());
  if (m_query) {
    SetHeader(wxString::Format(_("Searching for \"%s\" in %s: %zu matches ..."),
                               m_query->pattern, m_account.GetDisplayName(),
                               m_matchCount));
  }
}

void FindResultsView::OnDone(FindEvent &event) {
  if (event.GetInt() != m_generation) {
    return;
  }
  if (m_pending) {
    // The search that ended was stopped for this one.
    const Pending next = *m_pending;
    m_pending.reset();
    if (const Status started = m_search.Start(next.account, next.query);
        started.ok()) {
      Begin(next.account, next.query);
    } else {
      // The results of the search that was stopped are not those of this one.
      Clear();
      SetHeader(started.message());
    }
    return;
  }

  const wxString what =
      m_query ? wxString::Format(_("\"%s\""), m_query->pattern) : wxString();
  wxString header;
  if (!event.GetResult().ok()) {
    header = wxString::Format(_("%s: %s (%zu matches before it)"), what,
                              event.GetResult().message(), m_matchCount);
  } else {
    header = wxString::Format(_("%s: %zu matches in %zu files"), what,
                              m_matchCount, m_fileCount);
    if (event.IsCancelled()) {
      header += _(" (stopped)");
    } else if (event.IsTruncated()) {
      header += wxString::Format(_(" (only the first %zu)"),
                                 FindInFiles::kMaxMatches);
    }
  }
  SetHeader(header);
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

void FindResultsView::Append(const std::vector<FindMatch> &matches) {
  if (matches.empty()) {
    return;
  }

  struct Span {
    int style;
    int length;
  };
  struct Hit {
    int offset;
    int length;
  };
  wxString out;
  std::vector<Span> spans;
  std::vector<Hit> hits;
  int offset = 0; // In bytes, from the start of what is appended now

  const auto add = [&](const wxString &text, int style) {
    const int length = Utf8Length(text);
    out += text;
    spans.push_back({style, length});
    offset += length;
  };

  for (const FindMatch &match : matches) {
    if (match.path != m_lastPath) {
      m_lastPath = match.path;
      ++m_fileCount;
      add(Relative(match.path) + "\n", kStyleFile);
      m_rows.push_back({match.path, 0, 0});
    }
    ++m_matchCount;
    add(wxString::Format("  %d: ", match.line), kStyleLine);
    if (match.matchLength > 0) {
      hits.push_back(
          {offset + Utf8Length(match.text.Left(match.matchStart)),
           Utf8Length(match.text.Mid(match.matchStart, match.matchLength))});
    }
    add(match.text + "\n", wxSTC_STYLE_DEFAULT);
    m_rows.push_back({match.path, match.line, match.column});
  }

  const int start = m_text->GetLength();
  m_text->SetReadOnly(false);
  m_text->AppendText(out);
  m_text->StartStyling(start);
  for (const Span &span : spans) {
    m_text->SetStyling(span.length, span.style);
  }
  m_text->SetIndicatorCurrent(kIndicatorMatch);
  for (const Hit &hit : hits) {
    m_text->IndicatorFillRange(start + hit.offset, hit.length);
  }
  m_text->SetReadOnly(true);
}

void FindResultsView::SetHeader(const wxString &text) {
  m_header->SetLabel(text);
  m_header->SetToolTip(text);
  Layout();
}

// ---------------------------------------------------------------------------
// Opening a match
// ---------------------------------------------------------------------------

void FindResultsView::OnDoubleClick(wxStyledTextEvent &event) {
  Activate(event.GetLine());
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
  FileEvent open{wxEVT_FILETREE_ACTIVATED};
  open.SetEventObject(this);
  open.SetPath(row.path);
  open.SetIsDir(false);
  open.SetAccount(m_account);
  open.SetLocation(row.line, row.column);
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
  // The text is styled by hand (see Append()), not by a lexer.
  m_text->SetLexer(wxSTC_LEX_CONTAINER);
  // No line numbers: the lines of the file are in the text itself.
  m_text->SetMarginWidth(0, 0);
  m_text->SetMarginWidth(1, 0);
  m_text->SetWrapMode(wxSTC_WRAP_NONE);

  m_text->StyleSetForeground(kStyleFile, theme.linkColour);
  m_text->StyleSetBold(kStyleFile, true);
  m_text->StyleSetForeground(kStyleLine, theme.brightBlack);

  m_text->IndicatorSetStyle(kIndicatorMatch, wxSTC_INDIC_ROUNDBOX);
  m_text->IndicatorSetForeground(kIndicatorMatch, theme.highlightBg);
  m_text->IndicatorSetAlpha(kIndicatorMatch, 120);
  m_text->IndicatorSetOutlineAlpha(kIndicatorMatch, 200);
  m_text->IndicatorSetUnder(kIndicatorMatch, true);

  m_text->SetReadOnly(true);
  m_text->SetBackgroundColour(theme.bg);
  Refresh();
}
