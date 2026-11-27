#pragma once

#include "core/PageViewEvent.hpp"
#include "core/Status.h"
#include "core/lsp/LspClient.h"
#include "core/lsp/LspServerKey.h"

#include <map>
#include <memory>
#include <vector>
#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

// Where a server is on its way to being used.
enum class LspServerState {
  kChecking,        // Looking for the server on the machine
  kAwaitingConsent, // Not there: the user is asked if it may be installed
  kInstalling,      // Being downloaded and installed
  kInitializing,    // Launched, the handshake is not done
  kReady,           // Can be used
  kRestarting,      // The process ended: waiting to start it again
  kDeclined,        // The user said no to the install
  kFailed,          // Did not work (the message says why)
};

// The events of LspManager. They are sent on the EventNotifier, on the UI
// thread.
class LspManagerEvent : public wxCommandEvent {
public:
  explicit LspManagerEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  LspManagerEvent(const LspManagerEvent &) = default;
  wxEvent *Clone() const override { return new LspManagerEvent(*this); }

  // The server that the event is about.
  const LspServerKey &GetKey() const { return m_key; }
  void SetKey(const LspServerKey &key) { m_key = key; }

private:
  LspServerKey m_key;
};

// The server is not on the machine, and the user has to say if Kennel may
// install it: answer with LspManager::Install() or LspManager::Decline().
// GetString() is the name of the program and its version.
wxDECLARE_EVENT(wxEVT_LSP_INSTALL_NEEDED, LspManagerEvent);
// The server of GetKey() is ready: LspManager::GetServerFor() gives its client
// now.
wxDECLARE_EVENT(wxEVT_LSP_SERVER_READY, LspManagerEvent);
// A server could not be made ready. GetString() says why.
wxDECLARE_EVENT(wxEVT_LSP_SERVER_FAILED, LspManagerEvent);

// The owner of the language servers, and the only thing a page talks to. One
// server for each LspServerKey. GUI-free; only the UI thread may call it.
//
// One timer, for all the servers, looks once a second for a server whose
// process ended, and starts it again after a delay that doubles with every
// failure in a row (1 s, 2 s, 4 s ... at most 60 s). It gives up after 5
// failures in a row; a server that ran for 60 s starts the count again.
class LspManager : public wxEvtHandler {
public:
  static LspManager &Get();
  ~LspManager();

  LspManager(const LspManager &) = delete;
  LspManager &operator=(const LspManager &) = delete;

  // The client of the server of `key` when it is ready. Otherwise an error that
  // says why (StatusCode):
  //  - kUnavailable: it is not ready yet (a server that is not known yet is
  //    started now: it is checked, installed if the user agrees, launched and
  //    initialized); try again when it says that it is ready.
  //  - kPermissionDenied: the user did not want it installed.
  //  - kNotAvailable: there is no server of this kind to install.
  //  - kUnknown: it failed, the message says why.
  StatusOr<std::shared_ptr<LspClient>> GetServerFor(const LspServerKey &key);

  // Ends every server (a running one is stopped) and forgets them all. For the
  // end of the application.
  void Shutdown();

  // The answer to wxEVT_LSP_INSTALL_NEEDED: install the server, or do not.
  void Install(const LspServerKey &key);
  void Decline(const LspServerKey &key);

  // Starts to listen to the page events (wxEVT_PAGE_ADDED and the others): the
  // manager keeps what pages are open, and ends a server when no page needs it
  // any more. Call it once, before the first page is made.
  void Initialize();

private:
  LspManager() = default;

  // An open page, as the page events tell it. A server is needed while a page
  // matches it: a session or a terminal that works in the folder of the server
  // (same machine, the same working directory), or a file that is inside that
  // folder.
  struct Page {
    PageType m_type{PageType::kSession};
    // Account::GetId(): the machine.
    wxString m_machine;
    // A session or a terminal: its working directory. A file: the folder of the
    // file.
    wxString m_workingDir;
    // A file: its full path.
    wxString m_path;
    // A session or a terminal: its name.
    SessionRef m_ref;
  };

  void OnPageAdded(PageViewEvent &event);
  void OnPageRemoved(PageViewEvent &event);
  void OnPageRenamed(PageViewEvent &event);
  void OnPageMoved(PageViewEvent &event);
  void OnPagesCleared(wxCommandEvent &event);

  // Whether an open page needs the server of `key`.
  bool IsNeeded(const LspServerKey &key) const;
  // Ends the servers that no open page needs (what the user declined stays).
  // With `force` every server is ended, without looking at the pages.
  void EndUnusedServers(bool force = false);

  // One server of the manager (see LspManager.cpp).
  class Entry;

  // The timer: every entry looks at its process.
  void OnTimer(wxTimerEvent &event);

  // The key is LspServerKey::GetKey().
  std::map<wxString, std::unique_ptr<Entry>> m_entries;
  // The pages that are open.
  std::vector<Page> m_pages;
  // Made with the first server, ended by Shutdown().
  std::unique_ptr<wxTimer> m_timer;
  // Shutdown() was called: GetServerFor() starts nothing any more.
  bool m_isShutdown{false};
};
