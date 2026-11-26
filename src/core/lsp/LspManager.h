#pragma once

#include "core/Status.h"
#include "core/lsp/LspClient.h"
#include "core/lsp/LspServerKey.h"

#include <map>
#include <memory>
#include <wx/string.h>

// Where a server is on its way to being used.
enum class LspServerState {
  kChecking,        // Looking for the server on the machine
  kAwaitingConsent, // Not there: the user is asked if it may be installed
  kInstalling,      // Being downloaded and installed
  kInitializing,    // Started, the handshake (initialize) is not done
  kReady,           // Can be used
  kDeclined,        // The user said no to the install
  kFailed,          // Did not work (the message says why)
};

// The owner of the language servers, and the only thing a page talks to. One
// server for each LspServerKey. GUI-free; only the UI thread may call it.
class LspManager {
public:
  static LspManager &Get();

  LspManager(const LspManager &) = delete;
  LspManager &operator=(const LspManager &) = delete;

  // The client of the server of `key` when it is ready. Otherwise an error that
  // says why (StatusCode):
  //  - kUnavailable: it is not ready yet (a server that is not known yet is
  //    started now: it is checked, installed if the user agrees, launched and
  //    initialized); try again when it says that it is ready.
  //  - kPermissionDenied: the user did not want it installed.
  //  - kUnknown: it failed, the message says why.
  StatusOr<std::shared_ptr<LspClient>> GetServerFor(const LspServerKey &key);

private:
  LspManager() = default;

  // One server of the manager.
  struct Entry {
    LspServerState m_state{LspServerState::kChecking};
    // Set when the state is kReady.
    std::shared_ptr<LspClient> m_client;
    // Set when the state is kFailed.
    wxString m_message;
  };

  // The key is LspServerKey::GetKey().
  std::map<wxString, Entry> m_entries;
};
