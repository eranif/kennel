#pragma once

#include "app/FileEvent.hpp"
#include "app/PageViewEvent.hpp"
#include "core/Account.h"
#include "core/GitDriver.h"
#include "core/GitEvent.h"
#include "core/GitTypes.h"
#include "core/Workspace.h"

#include <memory>
#include <optional>
#include <thread>
#include <vector>
#include <wx/panel.h>

class wxAuiToolBar;
class wxDataViewEvent;
class wxDataViewListCtrl;
class wxStaticText;

// Sent on the EventNotifier when the user asks for the diff of a file:
// GetPath() is the file (in the notation of GetAccount()), GetInt() is 1 for
// the changes of the index (staged), GetString() is the diff. The host shows
// it.
wxDECLARE_EVENT(wxEVT_GITVIEW_SHOW_DIFF, FileEvent);

// The git changes of the session that is showing.
//
//   [ refresh ]
//   main  ahead 1, behind 0
//   Status               Path
//   Modified (staged)    src/app/a.cpp
//   Modified             src/app/a.cpp
//   Untracked            notes.txt
//
// A file that has changes in the index and in the work tree has two rows. A
// double click shows the diff of the row (wxEVT_GITVIEW_SHOW_DIFF); the context
// menu has Show Diff and Open File.
//
// Self contained, like FileExplorerView: it listens on the EventNotifier to
// which sessions exist and which one shows, keeps one GitDriver for each
// session (the kind comes from the Account of the session; it starts when the
// session is first shown), and sends the file to open
// (wxEVT_FILETREE_ACTIVATED) and the diff to show on the EventNotifier. A
// session without a working folder has no driver.
class GitView : public wxPanel {
public:
  explicit GitView(wxWindow *parent);
  ~GitView() override;

  // Gives the toolbar the style of the theme of the application.
  void ApplyToolBarStyle();

private:
  // A row of the table.
  struct Row {
    wxString path; // In the repository (empty: a message, not a file)
    bool staged{false};
    bool untracked{false};
    bool deleted{false};
  };

  // One session.
  struct Entry {
    SessionRef ref;
    Account account;
    bool hasFolder{false};
    // Null until the session is first shown, and for a session with no folder.
    std::unique_ptr<GitDriver> driver;
    // The last answer. `message` is what the table shows instead of the files
    // (an error, or "Loading...").
    GitStatus status;
    wxString message;
  };

  void OnSessionPageActivity(PageViewEvent &event);
  void OnSessionPageAdded(PageViewEvent &event);
  void OnSessionPageRemoved(PageViewEvent &event);
  void OnSessionRenamed(PageViewEvent &event);
  void OnSessionPageShown(PageViewEvent &event);
  void OnSessionPagesCleared(wxCommandEvent &event);
  void OnGitStatus(GitEvent &event);
  void OnFileSaved(FileEvent &event);
  void OnRefresh(wxCommandEvent &event);
  void OnActivated(wxDataViewEvent &event);
  void OnContextMenu(wxDataViewEvent &event);
  void OnMenuShowDiff(wxCommandEvent &event);
  void OnMenuOpenFile(wxCommandEvent &event);
  void OnGitDiff(GitEvent &event);

  void RefreshView();
  // The row of the table at `item`, null for a message.
  const Row *RowOf(const wxDataViewItem &item) const;
  // Asks for the diff of the file (the answer is OnGitDiff()).
  void ShowDiff(const Row &row);
  // Sends the file to the host to open.
  void OpenFile(const Row &row);

  int FindEntry(const SessionRef &ref) const;
  Entry *ActiveEntry();
  // Asks the driver of the entry for the status, if it has one.
  void Request(Entry &entry);
  // Deletes the driver on another thread (see the definition).
  void DeleteInBackground(std::unique_ptr<GitDriver> driver);
  // Fills the label and the table from the entry that shows.
  void Refill();

  wxAuiToolBar *m_toolbar{nullptr};
  wxStaticText *m_branch{nullptr};
  wxDataViewListCtrl *m_list{nullptr};
  std::vector<Entry> m_entries;
  // The threads that end the drivers of the sessions that went.
  std::vector<std::thread> m_reapers;
  std::optional<SessionRef> m_active;
  // The rows of the table.
  std::vector<Row> m_rows;
};
