#pragma once

#include "core/Account.h"

#include <cstdint>
#include <wx/event.h>
#include <wx/string.h>

// The number that LspManager gives to a request of an editor (an incrementing
// number, never used twice). The answer comes with the same number, and that
// is how the editor knows it is the owner of the event.
using RequestID = uint64_t;

// What every answer of the language server to an editor has: the request that
// it answers and the file that it is about. The answers (the semantic tokens,
// later the completions, the symbols, the outline) derive from it. Sent on the
// EventNotifier, on the UI thread. GUI-free.
class LspEvent : public wxCommandEvent {
public:
  explicit LspEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  LspEvent(const LspEvent &) = default;

  // The request that this event answers.
  RequestID GetRequestId() const { return m_requestId; }
  void SetRequestId(RequestID requestId) { m_requestId = requestId; }

  // The file that the request was about (IEditor::GetAccount() and
  // IEditor::GetFilePath() when it was made).
  const Account &GetAccount() const { return m_account; }
  void SetAccount(const Account &account) { m_account = account; }
  const wxString &GetFilePath() const { return m_filePath; }
  void SetFilePath(const wxString &path) { m_filePath = path; }

private:
  RequestID m_requestId{0};
  Account m_account;
  wxString m_filePath;
};
