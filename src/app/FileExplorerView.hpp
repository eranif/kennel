#pragma once

#include "app/FileEvent.hpp"
#include "app/FileTreeCtrl.hpp"
#include "app/PageViewEvent.hpp"
#include "core/Account.h"
#include "core/Workspace.h"

#include <optional>
#include <vector>
#include <wx/panel.h>

class wxAuiToolBar;
class wxSimplebook;

// The files of the open sessions: one FileTreeCtrl for each session, in a
// book, with a "Link with Session" tool above it.
//
//   [ link tool ]
//   +-----------------------------------------------+
//   |  the tree of the session that is showing      |
//   +-----------------------------------------------+
//
// While Link is on, the tree follows the session that is showing (see
// wxEVT_SESSION_PAGE_SHOWN); when it is off, the tree stays as it is.
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

  bool IsLinked() const;
  // The file `path` (in the notation of `account`: a Linux path for WSL) is
  // shown now. While Link is on, and when this view is on the screen, it is
  // selected in the tree that is showing, if the file is inside it (the path is
  // converted between this machine and WSL when the tree is of the other one).
  // Never sends an event, and never lists a remote host.
  void RevealFile(const Account &account, const wxString &path);
  // Gives the toolbar the style of the theme of the application (see
  // toolbar::ApplyStyle()). Called when the theme changes.
  void ApplyToolBarStyle();
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

  // The session the user is working in. While Link is on, its tree is shown.
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
  void OnLink(wxCommandEvent &event);

  wxAuiToolBar *m_toolbar{nullptr};
  wxSimplebook *m_book{nullptr};
  std::vector<Entry> m_entries;
  std::optional<SessionRef> m_active;
};
