#pragma once

#include "app/FileTreeCtrl.hpp"
#include "app/PageViewEvent.hpp"
#include "core/Account.h"
#include "core/Workspace.h"

#include <optional>
#include <vector>
#include <wx/panel.h>

class wxAuiToolBar;
class wxChoice;
class wxSimplebook;
class wxStaticText;

// Sent (to the parent windows) when the user picks a session in the choice. The
// session is GetRef(); the host shows it (and it goes back to the view with
// SetActiveSession(), which is harmless).
wxDECLARE_EVENT(wxEVT_FILEEXPLORER_SESSION_SELECTED, PageViewEvent);

// The files of the open sessions: one FileTreeCtrl for each session, in a
// book, and a choice (the sessions) with a "Link with Session" tool above it.
//
//   [ link tool ] [ choice of the sessions     v ]
//   +-----------------------------------------------+
//   |  the tree of the session in the choice        |
//   +-----------------------------------------------+
//
// While Link is on, the tree follows the session the host says is active
// (SetActiveSession()); the user can still choose another one in the choice.
//
// Self contained: the host (MainView) tells it which sessions exist, and
// listens to the events of the trees, which reach the parent windows (see
// FileTreeCtrl): it never learns what the host does with them. A session that
// lives on a remote host gets its tree when its entry is first shown: the
// connection takes seconds, so until then (and while it is made) the entry
// shows a message. A session that could not connect keeps the message.
class FileExplorerView : public wxPanel {
public:
  explicit FileExplorerView(wxWindow *parent);
  ~FileExplorerView() override;

  // A new session: its tree is built from `account` (the choice shows `label`).
  // The first session is shown at once.
  void AddSession(const SessionRef &ref, const wxString &label,
                  const Account &account);
  void RemoveSession(const SessionRef &ref);
  // A session got another name or group. Never loses the tree.
  void RenameSession(const SessionRef &from, const SessionRef &to,
                     const wxString &label);
  void Clear();

  // The session the user is working in. While Link is on, its tree is shown.
  // Never sends events.
  void SetActiveSession(const SessionRef &ref);

  bool IsLinked() const;
  // A file is shown now (the host's choice): while Link is on, and when this
  // view is on the screen, it is selected in the tree that is showing, if that
  // tree is of the account of the file (`path` as the tree writes it, see
  // FilePage::GetAccountPath()). Never changes the session in the choice, and
  // never lists a remote host: nothing happens for a remote account.
  void RevealFile(const Account &account, const wxString &path);
  // Gives the toolbar the style of the theme of the application (see
  // toolbar::ApplyStyle()). Called when the theme changes.
  void ApplyToolBarStyle();
  size_t GetSessionCount() const { return m_entries.size(); }

private:
  // One session: its entry in the choice, and its page in the book. The page
  // is the tree, or a message for a remote session that has no tree yet.
  struct Entry {
    int id{0}; // Never changes (the ref does when the session is renamed)
    SessionRef ref;
    wxString label;
    Account account;
    wxWindow *page{nullptr};
    FileTreeCtrl *tree{nullptr};
    wxStaticText *message{nullptr};
    bool connectStarted{false};
  };

  int FindEntry(const SessionRef &ref) const;
  int FindEntryById(int id) const;
  // A page that shows `text`; `label` is the text control, to change it later.
  wxWindow *NewMessagePage(const wxString &text, wxStaticText **label);
  // Makes the entry at `index` the one on the screen. A remote session that was
  // not connected yet is connected afterwards.
  void ShowEntry(int index);
  // Connects the remote session `id` and gives it its tree. It waits for the
  // host, so the entry shows a message before; the session may be gone after.
  void ConnectEntry(int id);
  void OnChoice(wxCommandEvent &event);
  void OnLink(wxCommandEvent &event);

  wxAuiToolBar *m_toolbar{nullptr};
  wxChoice *m_choice{nullptr};
  wxSimplebook *m_book{nullptr};
  // In the order of the choice.
  std::vector<Entry> m_entries;
  std::optional<SessionRef> m_active;
  int m_nextId{1};
};
