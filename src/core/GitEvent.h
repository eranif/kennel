#pragma once

#include "core/GitTypes.h"
#include "core/Status.h"

#include <wx/event.h>
#include <wx/string.h>

// The answer of a GitDriver. Sent on the EventNotifier, on the UI thread. A
// listener finds its driver by comparing GetInt() (the id of the driver that
// sends it) with GitDriver::GetId(). The event object is that driver too, but
// it may be gone when the event is handled: never use it, and never compare
// it (an address can be used again).
//
// GetResult() tells whether git worked. When it did not, the data is empty and
// GetResult().message() has the text git wrote.
class GitEvent : public wxCommandEvent {
public:
  explicit GitEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  GitEvent(const GitEvent &) = default;

  wxEvent *Clone() const override { return new GitEvent(*this); }

  const Status &GetResult() const { return m_result; }
  void SetResult(const Status &result) { m_result = result; }

  // wxEVT_GIT_STATUS
  const GitStatus &GetRepoStatus() const { return m_repoStatus; }
  void SetRepoStatus(const GitStatus &status) { m_repoStatus = status; }

  // wxEVT_GIT_BRANCH_NAME: the name. wxEVT_GIT_DIFF: the diff.
  const wxString &GetText() const { return m_text; }
  void SetText(const wxString &text) { m_text = text; }

  // wxEVT_GIT_DIFF: the file the diff is of.
  const wxString &GetPath() const { return m_path; }
  void SetPath(const wxString &path) { m_path = path; }

private:
  Status m_result;
  GitStatus m_repoStatus;
  wxString m_text;
  wxString m_path;
};

wxDECLARE_EVENT(wxEVT_GIT_STATUS, GitEvent);
wxDECLARE_EVENT(wxEVT_GIT_BRANCH_NAME, GitEvent);
wxDECLARE_EVENT(wxEVT_GIT_DIFF, GitEvent);
