#pragma once

#include <wx/event.h>

class MainNotebook;
class SessionPage;
class wxFrame;
class wxMenu;
class wxMenuBar;

// The Review Buddy menu:
//
//   Start Review Buddy...   (asks which agent reviews, in a dialog)
//   what the review buddy does
//   Send the Request Again, Stop the Review, Open the Latest Review
//   Close Review Buddy
//
// The menu is in the menu bar only while an agent session is showing (a file
// and a plain terminal have no review buddy). The items are always the same
// ones; which of them can be used, and what the status line says, comes from
// the session that is showing when the menu opens. They are the commands of the
// Review Buddy button of the session's toolbar.
class ReviewBuddyMenu : public wxEvtHandler {
public:
  // The shortcut of "Start Review Buddy...", for AcceleratorInterceptor (which
  // gets the key before a terminal does).
  static constexpr const char *kStartId = "review-start";

  // `frame` gets the events of the items; `book` has the pages. Both must stay
  // alive as long as this object.
  ReviewBuddyMenu(wxFrame *frame, MainNotebook *book);
  ~ReviewBuddyMenu() override;

  // Makes the menu. It is put into `menuBar` right after the menu `after` (a
  // menu of the bar) while an agent session shows, and taken out again
  // otherwise.
  void AddTo(wxMenuBar *menuBar, wxMenu *after);

  // For the commands that list the menus of the bar (it has its own entries
  // there, see MainFrame::CollectGotoItems()).
  const wxMenu *GetMenu() const { return m_menu; }

  // The session that is showing, if it can have a review buddy (an agent, not a
  // plain terminal).
  static SessionPage *AgentSessionOf(const MainNotebook *book);
  // Whether "Start Review Buddy..." can be used now: the session that is
  // showing can have a review buddy, and has none yet.
  static bool CanStart(const MainNotebook *book);

private:
  void OnItem(wxCommandEvent &event);
  void OnItemUI(wxUpdateUIEvent &event);
  // Adds the menu to the bar while an agent session is showing, and takes it
  // out otherwise.
  void OnIdle(wxIdleEvent &event);
  // Where the menu goes in the bar: after the menu `m_after`.
  size_t Position() const;

  wxFrame *m_frame;
  MainNotebook *m_book;
  wxMenuBar *m_menuBar{nullptr};
  wxMenu *m_after{nullptr};
  // The bar owns it while it is in the bar (m_menu->GetMenuBar() says).
  wxMenu *m_menu{nullptr};
};
