#include "core/lsp/LspManager.h"

#include <wx/intl.h>

LspManager &LspManager::Get() {
  static LspManager manager;
  return manager;
}

StatusOr<std::shared_ptr<LspClient>>
LspManager::GetServerFor(const LspServerKey &key) {
  const wxString name = key.m_account.GetDisplayName();
  auto [it, added] = m_entries.try_emplace(key.GetKey());
  const Entry &entry = it->second;
  if (added) {
    // New: it starts as kChecking.
    return Status::Error(
        StatusCode::kUnavailable,
        wxString::Format(_("Starting the language server on %s."), name));
  }

  switch (entry.m_state) {
  case LspServerState::kReady:
    return entry.m_client;
  case LspServerState::kDeclined:
    return Status::Error(
        StatusCode::kPermissionDenied,
        wxString::Format(_("The language server is not installed on %s."),
                         name));
  case LspServerState::kFailed:
    return Status::Error(entry.m_message);
  case LspServerState::kChecking:
  case LspServerState::kAwaitingConsent:
  case LspServerState::kInstalling:
  case LspServerState::kInitializing:
    break;
  }
  return Status::Error(
      StatusCode::kUnavailable,
      wxString::Format(_("The language server on %s is not ready yet."), name));
}
