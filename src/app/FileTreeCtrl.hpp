#pragma once

#include "app/FileProvider.hpp"
#include "core/Account.h"
#include "core/FileEvent.hpp"
#include "core/Status.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>
#include <wx/dataview.h>
#include <wx/panel.h>
#include <wx/utils.h>

// Sent by a FileTreeCtrl to its parent (and up; the event object is the tree)
// when something happens to a file or a folder of the tree. The FileEvent has
// the path, whether it is a folder (IsDir()) and the account of the tree.
//   wxEVT_FILETREE_SELECTED:  the selection changed (the user picked an entry)
//   wxEVT_FILETREE_ACTIVATED: double click or Enter on an entry. A folder also
//                             opens or closes in the tree; what else to do
//                             (open a file, cd into a folder) is up to the
//                             receiver
//   wxEVT_FILETREE_MENU:      the user asked for a context menu on an entry.
//                             The tree shows its own menu too (Copy Path,
//                             Open with Default App).
//   The
//                             path is empty when it was on empty space
wxDECLARE_EVENT(wxEVT_FILETREE_SELECTED, FileEvent);
wxDECLARE_EVENT(wxEVT_FILETREE_ACTIVATED, FileEvent);
wxDECLARE_EVENT(wxEVT_FILETREE_MENU, FileEvent);

// Sent on the EventNotifier (pending, the event object is the tree: compare the
// pointer only) when the user chose "Set This Folder as Root" for a folder of
// the tree: GetPath() is the folder, GetAccount() the account of the tree. The
// tree does not change by itself; the host decides (see SideView).
wxDECLARE_EVENT(wxEVT_FILETREE_SET_ROOT, FileEvent);

// Sent on the EventNotifier, from the worker thread of a tree, when it listed a
// folder (wxEVT_FILETREE_LISTED, the request id is the UID of the tree: a tree
// takes only its own) or when the request of FileTreeCtrl::ConnectAsync() is
// done (wxEVT_FILETREE_CONNECTED, each request has its own id, a number that
// only goes up: a listener handles only the request that it started). The
// listener skips the event in any case.
class DataViewTypeHelper;

class FileTreeConnectedEvent : public wxCommandEvent {
public:
  explicit FileTreeConnectedEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  wxEvent *Clone() const override { return new FileTreeConnectedEvent(*this); }

  int GetRequestId() const { return m_requestId; }
  void SetRequestId(int requestId) { m_requestId = requestId; }
  // The error when the host could not be reached.
  const Status &GetStatus() const { return m_status; }
  void SetStatus(const Status &status) { m_status = status; }
  // The folder that was listed.
  const wxString &GetDir() const { return m_dir; }
  void SetDir(const wxString &dir) { m_dir = dir; }
  const std::vector<FileProvider::File> &GetFiles() const { return m_files; }
  void SetFiles(std::vector<FileProvider::File> files) {
    m_files = std::move(files);
  }
  // wxEVT_FILETREE_LISTED: whether the folder is the top of the tree (the tree
  // is built again) or a folder in it.
  bool IsRoot() const { return m_isRoot; }
  void SetRoot(bool isRoot) { m_isRoot = isRoot; }
  // The folders that were open when the listing was asked for: they are opened
  // again, and listed, as their parent comes (see
  // FileTreeCtrl::RefreshFolder()).
  const std::set<wxString> &GetExpanded() const { return m_expanded; }
  void SetExpanded(const std::set<wxString> &expanded) {
    m_expanded = expanded;
  }

private:
  int m_requestId{0};
  Status m_status;
  wxString m_dir;
  std::vector<FileProvider::File> m_files;
  bool m_isRoot{false};
  std::set<wxString> m_expanded;
};
wxDECLARE_EVENT(wxEVT_FILETREE_CONNECTED, FileTreeConnectedEvent);
wxDECLARE_EVENT(wxEVT_FILETREE_LISTED, FileTreeConnectedEvent);

class FileTreeChangedEvent;

// The files of an Account (this machine, a WSL distribution or a remote host)
// as a tree: a folder is listed when it is opened. The tree shows what the
// FileProvider of the account lists. It knows nothing of the sessions or of
// MainView: the host puts it in a window, and listens to the events above.
//
// Everything that touches the files is done by one thread of the tree, one job
// at a time (connect, list a folder, make or delete a file, read the first line
// of the files): the UI thread only asks, and draws the answers that come as
// events on the EventNotifier. The first listing of a local or a WSL account is
// asked for once the control is shown on the screen. A remote account is
// listed by ConnectAsync() (the connection takes seconds).
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
  // Tells this tree from every other one (see FileTypesEvent::GetTreeUID()).
  int GetUID() const { return m_uid; }

  // Makes `dir` the top of the tree, when it is listed. An empty `dir` is the
  // default folder of the account.
  void SetRoot(const wxString &dir);
  // Lists the folders that are open again, and keeps them open.
  void Reload();

  // Connects to a remote host and lists its files on the thread of the tree.
  // The tree shows "Connecting..." until then. It returns at once, with the id
  // of the request. When the thread is done, it sends wxEVT_FILETREE_CONNECTED
  // with that id on the EventNotifier; pass the event to FinishConnect().
  int ConnectAsync();
  // Fills the tree from the event of ConnectAsync(). False when the host could
  // not be reached (the error is in the event).
  bool FinishConnect(const FileTreeConnectedEvent &event);

  // Selects the entry `path` (opening the folders on the way) and scrolls to
  // it. False when the path is not under the top of the tree. Never sends
  // events. A folder on the way that is not listed yet is listed by the thread
  // of the tree, and the selection happens when it is there: this returns at
  // once. A newer call (or a new listing of the tree) replaces the one that is
  // waiting.
  bool SelectPath(const wxString &path);

  // The selected entry, if any.
  std::optional<FileProvider::File> GetSelection() const;

  // Gives the files of `lines` (the start of the first line by path, see
  // FileTypesEvent) the icon of their type. A path that is not in the tree (any
  // more) is skipped.
  void ApplyFirstLines(const std::map<wxString, wxString> &lines);

private:
  struct Node; // What an item of the tree stands for

  // What the user chose to delete, from the items of the tree.
  struct DeleteTarget {
    wxString path;
    wxString parentDir;
    wxString compare; // ForCompare() of the path
    bool isFolder{false};

    // Whether this entry is inside the folder `folder` (which is deleted too).
    bool IsInside(const DeleteTarget &folder) const;
  };

  // The jobs of the thread of the tree, in the order they were asked for. The
  // destructor clears the queue, pushes the job that tells the thread to end,
  // and joins it. The queue is guarded by `m_jobMutex`.
  enum class JobType {
    Connect,
    ListRoot,
    ListDir,
    ReadFirstLines,
    MakeFolder,
    MakeFile,
    Remove,
    Shutdown,
  };
  struct Job {
    JobType type{JobType::Shutdown};
    wxString dir;                      // Connect, ListRoot, ListDir
    int requestId{0};                  // Connect
    std::set<wxString> expanded;       // ListRoot, ListDir
    std::vector<wxString> paths;       // ReadFirstLines, MakeFolder, MakeFile,
                                       // Remove
    std::vector<wxString> refreshDirs; // MakeFolder, MakeFile, Remove
    wxString selectPath;               // MakeFolder, MakeFile

    static Job MakeShutdownJob();
    static Job MakeConnectJob(int requestId, const wxString &dir);
    static Job MakeListJob(JobType type, const wxString &dir,
                           const std::set<wxString> &expanded);
    static Job MakeReadFirstLinesJob(const std::vector<wxString> &paths);
    static Job MakeCreateJob(JobType type, const wxString &path,
                             const wxString &parentDir);
    static Job MakeRemoveJob(const std::vector<wxString> &paths,
                             const std::vector<wxString> &refreshDirs);

    // Whether both jobs list the same folder in the same way.
    bool IsSameListing(const Job &other) const;
    // Makes or deletes `path`, by the type of the job.
    Status ApplyTo(FileProvider &provider, const wxString &path) const;
  };

  // The window and its events
  void CreateTree();
  void BindEvents();

  // The jobs
  void EnsureWorker();
  void PushJob(Job job);
  void EnqueueListing(const wxString &dir, const std::set<wxString> &expanded);
  void ListWorker(std::shared_ptr<FileProvider> provider);
  void RunJob(FileProvider &provider, const Job &job);
  void RunConnectJob(FileProvider &provider, const Job &job);
  void RunListJob(FileProvider &provider, const Job &job);
  void RunReadFirstLinesJob(FileProvider &provider, const Job &job);
  void RunChangeJob(FileProvider &provider, const Job &job);
  // Lists `dir` (the folder the provider starts in when it is empty) into the
  // event.
  void ListInto(FileProvider &provider, const wxString &dir,
                FileTreeConnectedEvent *event);

  // The answers, on the EventNotifier.
  void OnListed(FileTreeConnectedEvent &event);
  void OnChanged(FileTreeChangedEvent &event);

  // Asks for the top of the tree to be listed, with `expanded` open again.
  void LoadRoot(const wxString &dir, const std::set<wxString> &expanded);
  // Builds the tree from `files`, the listing of `dir`.
  void Fill(const wxString &dir, std::vector<FileProvider::File> files,
            const std::set<wxString> &expanded);
  // Puts the top item (the folder `m_root`) in the empty tree.
  wxDataViewItem AddTop();
  // Puts the entries of `files` in the folder `parent` and asks for the
  // listing of those of its folders that are in `expanded`.
  void AddNodes(const wxDataViewItem &parent,
                std::vector<FileProvider::File> files,
                const std::set<wxString> &expanded);
  // The folder of the event is listed: its entries go in (if they are not yet).
  void FillFolder(const FileTreeConnectedEvent &event);
  // Replaces the tree with one top item that shows `text` (not a file).
  void ShowTopText(const wxString &text);
  // Asks for the listing of the folder of `item` if it was not done yet, and
  // returns true then (the folder must not open before it is listed).
  bool LoadChildrenIfFirstTime(const wxDataViewItem &item);
  // Forgets the entries of the folder of `item`, and asks for them again. An
  // open folder is listed at once and stays open (with the folders in it that
  // were open); a closed one is listed when it is opened.
  void RefreshFolder(const wxDataViewItem &item);
  void AddNode(const wxDataViewItem &parent, const FileProvider::File &file);
  // Gives the item of `file` the icon of its type.
  void SetFileIcon(const wxDataViewItem &item, const FileProvider::File &file);
  // Gives the file `path` the icon of the type its first line tells.
  void ApplyFirstLine(const wxString &path, const wxString &line);
  // Asks the thread of the tree to read the first line of the files of `files`
  // that have no extension; it sends wxEVT_FILETREE_TYPES_RESOLVED when done.
  void RequestFirstLines(const std::vector<FileProvider::File> &files);

  // Asks for a name and has a folder (or an empty file) made in the folder of
  // `folder`, which is listed again with the new entry selected.
  void NewEntry(const wxDataViewItem &folder, bool isFolder);
  // The name of a new entry, or empty when the user cancelled or it is not a
  // valid name (said to the user).
  wxString AskEntryName(bool isFolder);
  // `name` in the folder `dir`.
  wxString JoinPath(const wxString &dir, const wxString &name) const;
  // Asks, then has the entry of `item` deleted (a folder with all in it). Its
  // folder is listed again.
  void DeleteEntry(const wxDataViewItem &item);
  // Like DeleteEntry(), for all of `items`, with one question.
  void DeleteEntries(const wxDataViewItemArray &items);
  std::vector<DeleteTarget> GetDeleteTargets(const wxDataViewItemArray &items);
  // An entry inside a selected folder goes with the folder.
  void RemoveNestedTargets(std::vector<DeleteTarget> *targets) const;
  bool ConfirmDelete(const std::vector<DeleteTarget> &targets);
  // The folders that hold the targets, the outer ones first.
  std::vector<wxString>
  GetParentDirs(const std::vector<DeleteTarget> &targets) const;

  // Sends wxEVT_FILETREE_ACTIVATED for each file of `items`.
  void OpenItems(const wxDataViewItemArray &items);
  // The context menu when several entries are selected.
  void ShowMultiSelectionMenu(const wxDataViewItemArray &items);
  wxDataViewItemArray SelectedItems() const;
  void CollectExpanded(const wxDataViewItem &item,
                       std::set<wxString> *expanded) const;
  Node *NodeOf(const wxDataViewItem &item) const;
  // The path of the top of the tree in the notation of the entries.
  wxString NormalizeRoot(const wxString &dir) const;
  // The last part of `path`, the label of the top of the tree.
  static wxString FolderNameOf(const wxString &path);
  // `path` with / for the separator and none at the end, for comparing.
  wxString ForCompare(const wxString &path) const;
  // Whether `path` is `dir` or is inside it (both from ForCompare()).
  bool IsSameOrInside(const wxString &dir, const wxString &path) const;
  void SendFileEvent(wxEventType type, const wxDataViewItem &item);

  // Goes down from the top to `m_revealTarget`, opening the folders; stops at
  // the first one that is not listed (OnListed() goes on) and selects the
  // target when it gets there.
  void Reveal();
  // Whether the entry of `item` is `m_revealTarget`.
  bool IsRevealTarget(const wxDataViewItem &item) const;
  // The child of the folder `item` that is `wanted` or holds it (the paths from
  // ForCompare()); invalid when there is none.
  wxDataViewItem FindChildOnWay(const wxDataViewItem &item,
                                const wxString &wanted) const;
  // The child of the folder `folder` that has the path `path`; invalid when
  // there is none.
  wxDataViewItem FindChildByPath(const wxDataViewItem &folder,
                                 const wxString &path) const;
  // Makes `item` the only selected entry, and shows it.
  void SelectOnly(const wxDataViewItem &item);
  // The item of the folder `dir`, when the tree is listed down to it; invalid
  // otherwise.
  wxDataViewItem FindOnWay(const wxString &dir) const;
  void OnPrefsChanged(wxCommandEvent &event);

  void OnExpanding(wxDataViewEvent &event);
  void OnSelectionChanged(wxDataViewEvent &event);
  void OnActivated(wxDataViewEvent &event);
  void OnContextMenu(wxDataViewEvent &event);

  Account m_account;
  // Used by the thread of the tree, one job at a time. The UI thread only asks
  // it for what never changes (the path separator).
  std::shared_ptr<FileProvider> m_provider;
  wxDataViewTreeCtrl *m_tree{nullptr};
  // Typing selects the next entry that starts with the letters.
  std::unique_ptr<DataViewTypeHelper> m_typeHelper;
  wxString m_root;
  // The path that SelectPath() is going to (in the form of ForCompare()); empty
  // when none is waited for.
  wxString m_revealTarget;
  // The busy cursor while the folder that the user opened is listed (see
  // OnExpanding()); it goes when a listing arrives (see FillFolder()).
  std::optional<wxBusyCursor> m_busy;

  std::mutex m_jobMutex;
  std::condition_variable m_jobReady;
  std::deque<Job> m_jobs;
  std::thread m_worker;
  // The destructor sets it: the thread stops reading files (a host that is slow
  // would keep the destructor waiting).
  std::atomic<bool> m_stopping{false};
  // Tells this tree from every other one, for as long as it lives. Put on the
  // answers of the thread (the request id of the event, the tree of the other
  // events), so that a tree takes only its own.
  const int m_uid;
};
