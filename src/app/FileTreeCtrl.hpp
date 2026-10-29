#pragma once

#include "app/FileEvent.hpp"
#include "app/FileProvider.hpp"
#include "core/Account.h"

#include <memory>
#include <optional>
#include <set>
#include <wx/dataview.h>
#include <wx/panel.h>

// Sent by a FileTreeCtrl to its parent (and up; the event object is the tree)
// when something happens to a file or a folder of the tree. The FileEvent has
// the path, whether it is a folder (IsDir()) and the account of the tree.
//   wxEVT_FILETREE_SELECTED:  the selection changed (the user picked an entry)
//   wxEVT_FILETREE_ACTIVATED: double click or Enter on an entry. A folder also
//                             opens or closes in the tree; what else to do
//                             (open a file, cd into a folder) is up to the
//                             receiver
//   wxEVT_FILETREE_MENU:      the user asked for a context menu on an entry.
//   The
//                             path is empty when it was on empty space
wxDECLARE_EVENT(wxEVT_FILETREE_SELECTED, FileEvent);
wxDECLARE_EVENT(wxEVT_FILETREE_ACTIVATED, FileEvent);
wxDECLARE_EVENT(wxEVT_FILETREE_MENU, FileEvent);

// The files of an Account (this machine, a WSL distribution or a remote host)
// as a tree: a folder is listed when it is opened. The tree shows what the
// FileProvider of the account lists; it never changes a file. It knows nothing
// of the sessions or of MainView: the host puts it in a window, and listens to
// the events above.
//
// The first listing of a local or a WSL account is done once the control is
// shown on the screen. A remote account is listed after Connect() (the
// connection takes seconds); opening a folder of a remote account waits for the
// answer of the host.
class FileTreeCtrl : public wxPanel {
public:
  // The tree starts in account.m_rootDir (the default folder of the kind of
  // account when it is empty).
  FileTreeCtrl(wxWindow *parent, const Account &account);
  ~FileTreeCtrl() override;

  const Account &GetAccount() const { return m_account; }
  // The folder at the top of the tree, as the tree shows it. Empty until the
  // first listing is done.
  const wxString &GetRoot() const { return m_root; }

  // Makes `dir` the top of the tree. An empty `dir` is the default folder of
  // the account.
  void SetRoot(const wxString &dir);
  // Lists the folders that are open again, and keeps them open.
  void Reload();

  // Connects to a remote host and shows its files (a local or a WSL account has
  // nothing to connect: this just lists). It blocks until the host answers, and
  // it tells the user when it cannot connect. False if that failed.
  bool Connect();

  // Selects the entry `path` (opening the folders on the way, which lists them)
  // and scrolls to it. False when the path is not under the top of the tree.
  // Never sends events.
  bool SelectPath(const wxString &path);

  // The selected entry, if any.
  std::optional<FileProvider::File> GetSelection() const;

private:
  struct Node; // What an item of the tree stands for

  // Builds the tree from `dir`, and opens the folders of `expanded` (the ones
  // that were open before a reload).
  void Load(const wxString &dir, const std::set<wxString> &expanded);
  // Lists the folder of `item` if it was not done yet.
  void LoadChildren(const wxDataViewItem &item);
  void AddNode(const wxDataViewItem &parent, const FileProvider::File &file);
  void CollectExpanded(const wxDataViewItem &item,
                       std::set<wxString> *expanded) const;
  void RestoreExpanded(const wxDataViewItem &item,
                       const std::set<wxString> &expanded);
  Node *NodeOf(const wxDataViewItem &item) const;
  // The path of the top of the tree in the notation of the entries.
  wxString NormalizeRoot(const wxString &dir) const;
  // `path` with / for the separator and none at the end, for comparing.
  wxString ForCompare(const wxString &path) const;
  // Whether `path` is `dir` or is inside it (both from ForCompare()).
  bool IsSameOrInside(const wxString &dir, const wxString &path) const;
  void SendFileEvent(wxEventType type, const wxDataViewItem &item);

  void OnExpanding(wxDataViewEvent &event);
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnActivated(wxDataViewEvent &event);
  void OnContextMenu(wxDataViewEvent &event);

  Account m_account;
  std::unique_ptr<FileProvider> m_provider;
  wxDataViewTreeCtrl *m_tree{nullptr};
  wxString m_root;
};
