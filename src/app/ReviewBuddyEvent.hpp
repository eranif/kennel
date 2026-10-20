#pragma once

#include <wx/event.h>
#include <wx/string.h>

// What a ReviewBuddy tells its event target (see
// ReviewBuddy::SetEventTarget()). The event object is the ReviewBuddy that sent
// it.
//
//   STARTED               The review begins: the request is being written.
//   WAITING_FOR_FINDINGS  The reviewer is working on a round.
//   ADDRESSING_FINDINGS   The main agent is addressing the comments of a round.
//   FINISHED              The review is over. GetString() is the result: it is
//                         clean, or the round limit was reached.
//   ERROR                 The review stalled. GetString() is the reason. The
//                         user can send the request again.
//   STOPPED               The user stopped the review.
//   CLOSE                 The user asked, in the dialog at the end of a review,
//                         to close the Review Buddy. GetString() is the
//                         ReviewBuddy::LoopId(); the owner closes it later
//                         (CallAfter), not inside the handler.
class ReviewBuddyEvent : public wxEvent {
public:
  explicit ReviewBuddyEvent(wxEventType type = wxEVT_NULL, int id = wxID_ANY)
      : wxEvent(id, type) {}

  wxEvent *Clone() const override { return new ReviewBuddyEvent(*this); }

  const wxString &GetString() const { return m_string; }
  void SetString(const wxString &string) { m_string = string; }

private:
  wxString m_string;
};

wxDECLARE_EVENT(wxEVT_REVIEW_BUDDY_STARTED, ReviewBuddyEvent);
wxDECLARE_EVENT(wxEVT_REVIEW_BUDDY_WAITING_FOR_FINDINGS, ReviewBuddyEvent);
wxDECLARE_EVENT(wxEVT_REVIEW_BUDDY_ADDRESSING_FINDINGS, ReviewBuddyEvent);
wxDECLARE_EVENT(wxEVT_REVIEW_BUDDY_FINISHED, ReviewBuddyEvent);
wxDECLARE_EVENT(wxEVT_REVIEW_BUDDY_ERROR, ReviewBuddyEvent);
wxDECLARE_EVENT(wxEVT_REVIEW_BUDDY_STOPPED, ReviewBuddyEvent);
wxDECLARE_EVENT(wxEVT_REVIEW_BUDDY_CLOSE, ReviewBuddyEvent);
