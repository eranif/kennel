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

// Sent on the EventNotifier to show the log or the blame of a file in a
// read-only page: GetPath() is the file (or the folder of the repository, for
// the log of all of it), GetInt() is 0 for the log and 1 for the blame,
// GetString() is the text.
wxDECLARE_EVENT(wxEVT_GITVIEW_SHOW_TEXT, FileEvent);

// Sent on the EventNotifier to run a command line in the terminal "Git":
// GetPath() is the folder to start it in, GetString() is the command line.
wxDECLARE_EVENT(wxEVT_GITVIEW_RUN_IN_TERMINAL, FileEvent);

// Sent on the EventNotifier when the user chose another folder for the git
// view: GetPath() is the folder (in the notation of GetAccount()). The event
// object is the panel; the event is pending, so the panel may be gone: compare
// the pointer only. The host decides what moves (see SideView).
wxDECLARE_EVENT(wxEVT_GITVIEW_CHANGE_FOLDER, FileEvent);

// The git changes of one place: the repository of an Account (this machine, a
// WSL distribution or a remote host) and its working folder.
//
//   [ refresh ] [ folder ] [ add ] [ commit ] [ push ] [ log ] [ reset ]
//   main  ahead 1, behind 0
//   Status               Path
//   Modified (staged)    src/app/a.cpp
//   Modified             src/app/a.cpp
//   Untracked            notes.txt
//
// A file that has changes in the index and in the work tree has two rows. A
// double click shows the diff of the row (wxEVT_GITVIEW_SHOW_DIFF); the context
// menu has the actions of the files.
//
// A panel knows nothing of the sessions: SideView makes one for each place and
// shows the right one. It has one GitDriver (the kind comes from the Account;
// it starts with the first Activate()), listens on the EventNotifier to the
// answers of its own driver, and sends the file to open
// (wxEVT_FILETREE_ACTIVATED), the texts to show and the commands for the
// terminal on the EventNotifier. An account without a working folder has no
// driver.
class GitPanel : public wxPanel {
public:
  GitPanel(wxWindow *parent, const Account &account);
  ~GitPanel() override;

  const Account &GetAccount() const { return m_account; }

  // Reads the status of the repository (the answer comes later). Call it when
  // the panel is shown, and when the files may have changed.
  void Activate();
  // Gives the driver away, for the caller to end it on another thread (see
  // the definition): a panel that is about to be deleted does it.
  std::unique_ptr<GitDriver> ReleaseDriver();
  // The panel shows the repository of another folder of the same account. The
  // driver of the folder it had is given away like ReleaseDriver() does. Call
  // Activate() to read the new one.
  std::unique_ptr<GitDriver> SetRootDir(const wxString &dir);

  // Gives the toolbar the style of the theme of the application.
  void ApplyToolBarStyle();

private:
  // A row of the table.
  struct Row {
    wxString path; // In the repository (empty: a message, not a file)
    bool staged{false};
    bool untracked{false};
    bool deleted{false};
    bool added{false}; // New in the index
  };

  void OnGitStatus(GitEvent &event);
  void OnRefresh(wxCommandEvent &event);
  void OnActivated(wxDataViewEvent &event);
  void OnContextMenu(wxDataViewEvent &event);
  void OnGitCommand(GitEvent &event);
  // The actions of the context menu and of the toolbar.
  void OnMenuAction(wxCommandEvent &event);
  void OnGitDiff(GitEvent &event);
  // Asks for a folder with the file browser (any kind of host) and tells the
  // host with wxEVT_GITVIEW_CHANGE_FOLDER.
  void ChangeFolder();

  // The row of the table at `item`, null for a message.
  const Row *RowOf(const wxDataViewItem &item) const;
  // Asks for the diff of the file (the answer is OnGitDiff()).
  void ShowDiff(const Row &row);
  // Sends the file to the host to open.
  void OpenFile(const Row &row);
  // Opens the file with the application of the system (this machine and WSL).
  void OpenWithDefaultApp(const Row &row);
  // The rows that are selected (not the messages).
  std::vector<const Row *> SelectedRows() const;
  // The pathspecs of `rows`, for the arguments of git (after "--"), one for
  // each file.
  static std::vector<wxString> PathSpecs(const std::vector<const Row *> &rows);
  void AddFiles(const std::vector<const Row *> &rows);
  void ResetFiles(const std::vector<const Row *> &rows);
  void ResetRepo();
  // The log of the file of `row`, or of the repository when it is null.
  void ShowLog(const Row *row);
  void ShowBlame(const Row &row);
  void Commit();
  // Runs `git <command>` (push, pull) in the terminal of the account, where
  // git can ask for a password.
  void RunInTerminal(const wxString &command);

  // Fills the label and the table from the last answer.
  void Refill();
  // Makes the columns as wide as their content.
  void AutoSizeColumns();

  Account m_account;
  bool m_hasFolder{false};
  // Null until the first Activate(), and for an account with no folder.
  std::unique_ptr<GitDriver> m_driver;
  // The last answer. `m_message` is what the table shows instead of the files
  // (an error, or "Loading...").
  GitStatus m_status;
  wxString m_message;

  wxAuiToolBar *m_toolbar{nullptr};
  wxStaticText *m_branch{nullptr};
  wxDataViewListCtrl *m_list{nullptr};
  // The rows of the table.
  std::vector<Row> m_rows;
  // How many rows of the log to ask for.
  long m_logLines{100};
};
