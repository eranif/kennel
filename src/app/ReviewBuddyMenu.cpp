#include "app/ReviewBuddyMenu.hpp"

#include "app/MainNotebook.hpp"
#include "app/SessionPage.hpp"

#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/xrc/xmlres.h>

namespace {
constexpr const char *kStatusId = "review-status";
constexpr const char *kResendId = "review-resend";
constexpr const char *kStopId = "review-stop";
constexpr const char *kOpenLatestId = "review-open-latest";
constexpr const char *kCloseId = "review-close";
} // namespace

ReviewBuddyMenu::ReviewBuddyMenu(wxFrame *frame, MainNotebook *book)
    : m_frame{frame}, m_book{book} {}

ReviewBuddyMenu::~ReviewBuddyMenu() {
  // Not in the bar: nobody else owns it.
  if (m_menu != nullptr && m_menu->GetMenuBar() == nullptr) {
    delete m_menu;
  }
}

SessionPage *ReviewBuddyMenu::AgentSessionOf(const MainNotebook *book) {
  SessionPage *page = book->GetActiveSessionPage();
  return page != nullptr && !page->IsPlainTerminal() ? page : nullptr;
}

bool ReviewBuddyMenu::CanStart(const MainNotebook *book) {
  const SessionPage *page = AgentSessionOf(book);
  return page != nullptr && page->SupportsReviewBuddy() &&
         !page->HasReviewBuddy();
}

void ReviewBuddyMenu::AddTo(wxMenuBar *menuBar, wxMenu *after) {
  m_menuBar = menuBar;
  m_after = after;
  m_menu = new wxMenu();
  // The key is handled by AcceleratorInterceptor on Windows and GTK (a terminal
  // would get it otherwise); the text after the tab shows it.
  m_menu->Append(XRCID(kStartId), _("Start Review Buddy...\tCtrl+Shift+R"),
                 _("Choose the agent that reviews this session"));
  m_menu->AppendSeparator();
  m_menu->Append(XRCID(kStatusId), _("No review buddy is open"));
  m_menu->Append(XRCID(kResendId), _("Send the Request Again"));
  m_menu->Append(XRCID(kStopId), _("Stop the Review"));
  m_menu->Append(XRCID(kOpenLatestId), _("Open the Latest Review"));
  m_menu->AppendSeparator();
  m_menu->Append(XRCID(kCloseId), _("Close Review Buddy"));
  for (const char *name :
       {kStartId, kResendId, kStopId, kOpenLatestId, kCloseId}) {
    m_frame->Bind(wxEVT_MENU, &ReviewBuddyMenu::OnItem, this, XRCID(name));
  }
  for (const char *name :
       {kStartId, kStatusId, kResendId, kStopId, kOpenLatestId, kCloseId}) {
    m_frame->Bind(wxEVT_UPDATE_UI, &ReviewBuddyMenu::OnItemUI, this,
                  XRCID(name));
  }
  // No session is showing yet: the menu stays out of the bar until one is.
  m_frame->Bind(wxEVT_IDLE, &ReviewBuddyMenu::OnIdle, this);
}

size_t ReviewBuddyMenu::Position() const {
  for (size_t i = 0; i < m_menuBar->GetMenuCount(); ++i) {
    if (m_menuBar->GetMenu(i) == m_after) {
      return i + 1;
    }
  }
  return m_menuBar->GetMenuCount();
}

void ReviewBuddyMenu::OnIdle(wxIdleEvent &event) {
  event.Skip();
  const bool wanted = AgentSessionOf(m_book) != nullptr;
  const bool inBar = m_menu->GetMenuBar() != nullptr;
  if (wanted == inBar) {
    return;
  }
  if (wanted) {
    m_menuBar->Insert(Position(), m_menu, _("&Review Buddy"));
  } else {
    // Found by the menu, not by a place: the place changes when another menu
    // (Edit) comes and goes.
    for (size_t i = 0; i < m_menuBar->GetMenuCount(); ++i) {
      if (m_menuBar->GetMenu(i) == m_menu) {
        m_menuBar->Remove(i);
        break;
      }
    }
  }
}

void ReviewBuddyMenu::OnItem(wxCommandEvent &event) {
  SessionPage *page = AgentSessionOf(m_book);
  if (page == nullptr) {
    return;
  }
  const int id = event.GetId();
  if (id == XRCID(kStartId)) {
    // After this event: it shows a dialog.
    page->CallAfter(&SessionPage::ChooseReviewBuddy);
  } else if (id == XRCID(kResendId)) {
    page->ResendReview();
  } else if (id == XRCID(kStopId)) {
    page->StopReview();
  } else if (id == XRCID(kOpenLatestId)) {
    page->OpenLatestReview();
  } else if (id == XRCID(kCloseId)) {
    page->StopReviewBuddy(); // Closes it, after this event
  }
}

void ReviewBuddyMenu::OnItemUI(wxUpdateUIEvent &event) {
  const SessionPage *page = AgentSessionOf(m_book);
  if (page == nullptr) {
    event.Enable(false);
    return;
  }
  const int id = event.GetId();
  if (id == XRCID(kStartId)) {
    // Whether there is a git repository and an agent to review with is found
    // out when the item is used: it looks for a repository, which is not done
    // at every update of the menu.
    event.Enable(CanStart(m_book));
  } else if (id == XRCID(kStatusId)) {
    event.Enable(false);
    event.SetText(page->HasReviewBuddy()
                      ? _("Review Buddy: ") + page->DescribeReviewBuddy()
                      : _("No review buddy is open"));
  } else if (id == XRCID(kResendId)) {
    event.Enable(page->IsReviewBuddyLive() && page->CanResendReview());
  } else if (id == XRCID(kStopId)) {
    event.Enable(page->IsReviewBuddyLive());
  } else if (id == XRCID(kOpenLatestId)) {
    event.Enable(page->HasLatestReview());
  } else if (id == XRCID(kCloseId)) {
    event.Enable(page->HasReviewBuddy());
  }
}
