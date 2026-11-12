#pragma once

#include "UI.hpp"
#include "app/FileEvent.hpp"
#include "app/FileTreeCtrl.hpp"
#include "app/PageViewEvent.hpp"
#include "core/Account.h"
#include "core/Workspace.h"

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

class GitDriver;
class GitPanel;
class wxSimplebook;

// The content of the side bar: a book with the file explorer and the git view.
// MainView hosts it and knows nothing of the book; the tabs are only reached
// through the functions below.
//
// Both tabs show the files of one place, the "context": an Account (this
// machine, a WSL distribution or a remote host) and a folder. A context has a
// FileTreeCtrl (the file explorer) and a GitPanel (the git view), made and
// deleted together. Every session belongs to a context, and several sessions
// of the same place share one. SideView listens on the EventNotifier to which
// sessions exist and which one shows (see PageViewEvent.hpp), and shows the
// tree and the panel of the context of that session in both tabs.
//
// The user can choose another folder for both views (the folder button of the
// git view, or "Set This Folder as Root"
// in the menu of the tree). See ChangeFolder().
//
// A remote tree is connected when its context is first shown (see
// FileTreeCtrl::ConnectAsync()); a git panel reads its repository when its
// context is shown.
class SideView : public SideViewBase {
public:
  explicit SideView(wxWindow *parent);
  ~SideView() override;

  // The index of the tab that shows, or wxNOT_FOUND.
  int GetSelection() const;
  // Shows the tab `index`. An index that is not a tab is ignored.
  void SetSelection(int index);

  // Takes the theme, the outline colour of the tabs and the position of the
  // tabs from the prefs (call it when they change).
  void ApplyPrefs();
  // Gives the toolbars of the views the style of the theme of the application.
  void ApplyToolBarStyle();

private:
  // One place. Lives while a session belongs to it.
  struct Context {
    Account account;
    // The sessions that belong to the place.
    std::vector<SessionRef> sessions;
    FileTreeCtrl *tree{nullptr};
    GitPanel *git{nullptr};
    // A remote tree: the id of the request while it connects (see
    // FileTreeCtrl::ConnectAsync()), and whether it was started.
    int connectRequestId{0};
    bool connectStarted{false};
    // The context of a terminal: it belongs to that terminal alone and goes
    // with the terminal to the folders it visits (see OnSessionPageMoved()).
    bool isTerminal{false};
  };

  // Identifies the place of `account`: the account and its folder.
  static wxString KeyOf(const Account &account);
  // The context the session `ref` belongs to, or null.
  Context *FindContext(const SessionRef &ref);
  // The context that shows, or null.
  Context *ShownContext();

  void AddSession(const SessionRef &ref, const Account &account);
  void RemoveSession(const SessionRef &ref);
  void RenameSession(const SessionRef &from, const SessionRef &to);
  // Deletes the context under `key`. The git driver ends on another thread.
  // When it was the one that shows, another one is shown, unless `reshow` is
  // false (Clear() deletes them all).
  void DeleteContext(const wxString &key, bool reshow = true);
  void Clear();
  // Shows the tree and the panel of `context` in their tabs. A remote tree
  // that is not connected yet starts to connect.
  void ShowContext(Context &context);
  // Ends a git driver on another thread (see the definition).
  void ReapDriver(std::unique_ptr<GitDriver> driver);
  // Waits for the threads that end the drivers.
  void JoinReapers();
  // The user chose `dir` for the tree and the git panel of the context under
  // `key`. When the context has one session (and its tree can be used: a
  // remote one must be connected), it moves to `dir`. Otherwise the session the
  // user works in gets a new context (a new tree and a new panel) at `dir`, and
  // the others keep the old one. The session no longer shares the place with
  // the sessions that start in the old folder: the choice lives until the
  // session ends.
  void ChangeFolder(const wxString &key, const wxString &dir);

  void OnSessionPageAdded(PageViewEvent &event);
  void OnSessionPageRemoved(PageViewEvent &event);
  void OnSessionRenamed(PageViewEvent &event);
  void OnSessionPageShown(PageViewEvent &event);
  void OnSessionPagesCleared(wxCommandEvent &event);
  void OnSessionPageActivity(PageViewEvent &event);
  void OnSessionPageMoved(PageViewEvent &event);
  void OnChangeFolder(FileEvent &event);
  void OnFileSaved(FileEvent &event);
  void OnFileReveal(FileEvent &event);
  void OnTreeConnected(FileTreeConnectedEvent &event);
  // The settings dialog shows a tab height before it is saved.
  void OnTabHeightPreview(wxCommandEvent &event);

  wxSimplebook *m_explorerBook{nullptr}; // The trees
  wxSimplebook *m_gitBook{nullptr};      // The git panels
  // By KeyOf(). A node never moves: the pages keep pointers to it.
  std::map<wxString, Context> m_contexts;
  // The session the user is working in (its context shows).
  std::optional<SessionRef> m_active;
  // The number of the next context of a terminal (for its key).
  int m_nextTerminalId{0};
  // A thread that ends the driver of a context that went, and whether it is
  // done.
  struct Reaper {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };
  std::vector<Reaper> m_reapers;
};
