#include "app/ReviewBuddyHeader.hpp"

#include "app/ReviewBuddy.hpp"

#include "terminal_view.h"

namespace {
// Braille orbit: ⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧
const wchar_t kSpinnerFrames[] = {L'⠋', L'⠙', L'⠹', L'⠸',
                                  L'⠼', L'⠴', L'⠦', L'⠧'};
static_assert(sizeof(kSpinnerFrames) / sizeof(kSpinnerFrames[0]) ==
              Spinner::kFrameCount);

// In front of the final status: the review is clean, it failed, or the round
// limit was reached (findings are still open).
const wchar_t kCleanMark[] = L"👍"; // Thumbs up
const wchar_t kErrorMark[] = L"👎"; // Thumbs down
const wchar_t kLimitMark[] = L"🔴"; // Red circle

const wxEventTypeTag<ReviewBuddyEvent> *const kEventTypes[] = {
    &wxEVT_REVIEW_BUDDY_STARTED,
    &wxEVT_REVIEW_BUDDY_WAITING_FOR_FINDINGS,
    &wxEVT_REVIEW_BUDDY_ADDRESSING_FINDINGS,
    &wxEVT_REVIEW_BUDDY_FINISHED,
    &wxEVT_REVIEW_BUDDY_ERROR,
    &wxEVT_REVIEW_BUDDY_STOPPED,
};
} // namespace

ReviewBuddyHeader::ReviewBuddyHeader(wxTerminalViewCtrl *terminal,
                                     wxEvtHandler *source)
    : m_terminal(terminal), m_source(source),
      m_spinner([this](const wxBitmapBundle &) { OnSpinnerFrame(); }) {
  for (const auto *type : kEventTypes) {
    m_source->Bind(*type, &ReviewBuddyHeader::OnReviewEvent, this);
  }
}

ReviewBuddyHeader::~ReviewBuddyHeader() {
  for (const auto *type : kEventTypes) {
    m_source->Unbind(*type, &ReviewBuddyHeader::OnReviewEvent, this);
  }
  // The header is not touched: the terminal may be gone already.
}

void ReviewBuddyHeader::SetText(wxTerminalViewCtrl *terminal,
                                const wxString &text, bool force) {
  if (terminal == nullptr) {
    return;
  }
  // On a dark background the text is yellow on a darker strip; otherwise the
  // strip has the colours of the theme.
  const wxColour &bg = terminal->GetTheme().bg;
  const bool dark =
      (bg.Red() * 299 + bg.Green() * 587 + bg.Blue() * 114) / 1000 < 128;
  const wxColour fg = dark ? wxColour(0xE6, 0xC8, 0x4C) : wxNullColour;
  const wxColour stripBg = dark ? bg.ChangeLightness(60) : wxNullColour;

  // Not again for the same text: the strip is laid out each time.
  if (force || !terminal->HasHeader() || terminal->GetHeaderText() != text) {
    terminal->AddHeader(text, fg, stripBg);
  }
}

void ReviewBuddyHeader::OnReviewEvent(ReviewBuddyEvent &event) {
  // Not handled: the owner of the review buddy needs it too.
  event.Skip();

  const wxEventType type = event.GetEventType();
  m_spinning = type == wxEVT_REVIEW_BUDDY_STARTED ||
               type == wxEVT_REVIEW_BUDDY_WAITING_FOR_FINDINGS ||
               type == wxEVT_REVIEW_BUDDY_ADDRESSING_FINDINGS;

  // The mark of the final status.
  m_mark.clear();
  if (type == wxEVT_REVIEW_BUDDY_FINISHED) {
    auto *finished = dynamic_cast<ReviewBuddy *>(event.GetEventObject());
    m_mark =
        finished != nullptr && finished->IsClean() ? kCleanMark : kLimitMark;
  } else if (type == wxEVT_REVIEW_BUDDY_ERROR) {
    m_mark = kErrorMark;
  }

  // The sender knows the round and whether it is slow.
  if (auto *buddy = dynamic_cast<ReviewBuddy *>(event.GetEventObject())) {
    m_text = buddy->StatusText();
  } else if (type == wxEVT_REVIEW_BUDDY_ERROR) {
    m_text = wxString::Format(_("Review stalled: %s"), event.GetString());
  } else {
    m_text = _("Review Buddy");
  }

  if (m_spinning) {
    m_spinner.Start();
  } else {
    m_spinner.Stop();
  }
  Render();
}

void ReviewBuddyHeader::OnSpinnerFrame() {
  // Not drawn while the page is hidden: the strip is laid out each time.
  if (m_terminal != nullptr && m_terminal->IsShownOnScreen()) {
    Render();
  }
}

void ReviewBuddyHeader::Render(bool force) {
  wxString text;
  if (m_spinning) {
    text << wxString(kSpinnerFrames[m_spinner.GetFrame()]) << " ";
  } else if (!m_mark.empty()) {
    text << m_mark << " ";
  }
  text << m_text;
  SetText(m_terminal, text, force);
}
