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
// double click opens the file.
//
// Self contained, like FileExplorerView: it listens on the EventNotifier to
// which sessions exist and which one shows, keeps one GitDriver for each
// session (the kind comes from the Account of the session; it starts when the
// session is first shown), and sends the file
// to open on the EventNotifier (wxEVT_FILETREE_ACTIVATED). A session without a
// working folder has no driver.
class GitView : public wxPanel {
public:
  explicit GitView(wxWindow *parent);
  ~GitView() override;

  // Gives the toolbar the style of the theme of the application.
  void ApplyToolBarStyle();

private:
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

  void OnSessionPageAdded(PageViewEvent &event);
  void OnSessionPageRemoved(PageViewEvent &event);
  void OnSessionRenamed(PageViewEvent &event);
  void OnSessionPageShown(PageViewEvent &event);
  void OnSessionPagesCleared(wxCommandEvent &event);
  void OnGitStatus(GitEvent &event);
  void OnFileSaveDone(FileEvent &event);
  void OnRefresh(wxCommandEvent &event);
  void OnActivated(wxDataViewEvent &event);

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
  // The file of each row of the table (its path in the repository), empty for
  // a row that is not a file.
  std::vector<wxString> m_rowPaths;
};
