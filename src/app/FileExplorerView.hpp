#pragma once

#include "app/FileEvent.hpp"
#include "app/FileTreeCtrl.hpp"
#include "app/PageViewEvent.hpp"
#include "core/Account.h"
#include "core/Workspace.h"

#include <optional>
#include <vector>
#include <wx/panel.h>

class wxSimplebook;

// The files of the open sessions: one FileTreeCtrl for each session, in a
// book. The tree of the session that is showing is on the screen (see
// wxEVT_SESSION_PAGE_SHOWN).
//
// Self contained: it listens on the EventNotifier to which sessions exist, and
// to the events of the trees, which reach the parent windows (see
// FileTreeCtrl): it never learns what the host does with them. The tree of a
// session that lives on a remote host connects when its entry is first shown
// (see FileTreeCtrl::ConnectAsync()).
class FileExplorerView : public wxPanel {
public:
  explicit FileExplorerView(wxWindow *parent);
  ~FileExplorerView() override;

  // The file `path` (in the notation of `account`: a Linux path for WSL) is
  // shown now. When this view is on the screen, it is
  // selected in the tree that is showing, if the file is inside it (the path is
  // converted between this machine and WSL when the tree is of the other one).
  // Never sends an event, and never lists a remote host.
  void RevealFile(const Account &account, const wxString &path);
  size_t GetSessionCount() const { return m_entries.size(); }

private:
  void OnFileReveal(FileEvent &event);
  void OnSessionPageShown(PageViewEvent &event);
  void OnSessionRenamed(PageViewEvent &event);
  void OnSessionPageAdded(PageViewEvent &event);
  void OnSessionPageRemoved(PageViewEvent &event);
  void OnSessionPagesCleared(wxCommandEvent &event) {
    event.Skip();
    Clear();
  }

  // The session the user is working in. Its tree is shown.
  // Never sends events.
  void SetActiveSession(const SessionRef &ref);
  void Clear();
  void RemoveSession(const SessionRef &ref);

  // A new session: its tree is built from `account`. The first session is shown
  // at once.
  void AddSession(const SessionRef &ref, const Account &account);

  // A session got another name or group. Never loses the tree.
  void RenameSession(const SessionRef &from, const SessionRef &to);

  // One session: its tree (the page in the book).
  struct Entry {
    SessionRef ref;
    Account account;
    FileTreeCtrl *tree{nullptr};
    // A remote tree: the id of the request while it connects (see
    // FileTreeCtrl::ConnectAsync()), and whether it was started.
    int connectRequestId{0};
    bool connectStarted{false};
  };

  int FindEntry(const SessionRef &ref) const;
  // The entry whose tree is on the screen, or wxNOT_FOUND.
  int ShownIndex() const;
  // Makes the entry at `index` the one on the screen. A remote session that is
  // not connected yet starts to connect (in the background).
  void ShowEntry(int index);
  void OnTreeConnected(FileTreeConnectedEvent &event);

  wxSimplebook *m_book{nullptr};
  std::vector<Entry> m_entries;
  std::optional<SessionRef> m_active;
};
