#include "app/FileTreeCtrl.hpp"

#include "app/DataViewTypeHelper.h"
#include "app/DataViewUtils.hpp"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Logger.h"
#include "core/WslPath.h"

#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

#include <algorithm>
#include <optional>
#include <thread>

wxDEFINE_EVENT(wxEVT_FILETREE_SELECTED, FileEvent);
wxDEFINE_EVENT(wxEVT_FILETREE_ACTIVATED, FileEvent);
wxDEFINE_EVENT(wxEVT_FILETREE_MENU, FileEvent);
wxDEFINE_EVENT(wxEVT_FILETREE_SET_ROOT, FileEvent);
wxDEFINE_EVENT(wxEVT_FILETREE_CONNECTED, FileTreeConnectedEvent);
wxDEFINE_EVENT(wxEVT_FILETREE_LISTED, FileTreeConnectedEvent);

namespace {
// The id of the last request of ConnectAsync(). Only the UI thread uses it.
int s_lastRequestId = 0;

const int kIdCopyPath = wxWindow::NewControlId();
const int kIdOpenDefault = wxWindow::NewControlId();
const int kIdRefresh = wxWindow::NewControlId();
const int kIdSetRoot = wxWindow::NewControlId();
const int kIdNewFolder = wxWindow::NewControlId();
const int kIdNewFile = wxWindow::NewControlId();
const int kIdDelete = wxWindow::NewControlId();
const int kIdOpen = wxWindow::NewControlId();
const int kIdShowInFileManager = wxWindow::NewControlId();

// Puts `text` on the clipboard.
void CopyText(const wxString &text) {
  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Close();
  }
}

// "Show in Finder" and the like: the file manager of this system.
wxString ShowInFileManagerLabel() {
#if defined(__WXMAC__)
  return _("Show in Finder");
#elif defined(__WXMSW__)
  return _("Show in Explorer");
#else
  return _("Show in File Manager");
#endif
}

// Shows `path` (a path of this machine) in the file manager, selected where the
// system can do it.
void ShowInFileManager(const wxString &path) {
#if defined(__WXMAC__)
  const wchar_t *argv[] = {L"open", L"-R", path.wc_str(), nullptr};
  ::wxExecute(argv, wxEXEC_ASYNC);
#elif defined(__WXMSW__)
  ::wxExecute(wxString::Format("explorer.exe /select,\"%s\"",
                               wxFileName(path).GetFullPath()),
              wxEXEC_ASYNC);
#else
  // No standard way to select it: the folder that holds it.
  ::wxLaunchDefaultApplication(wxFileName(path).GetPath());
#endif
}

// Whether `name` can be the name of a new entry: not empty, one part of a path.
bool IsValidEntryName(const wxString &name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of("/\\\r\n") == wxString::npos;
}

// Folders first, then by name, ignoring the case.
void SortFiles(std::vector<FileProvider::File> *files) {
  std::sort(files->begin(), files->end(),
            [](const FileProvider::File &a, const FileProvider::File &b) {
              if (a.IsFolder() != b.IsFolder()) {
                return a.IsFolder();
              }
              return a.path.CmpNoCase(b.path) < 0;
            });
}
} // namespace

// What an item of the tree stands for. The tree owns it (and deletes it with
// the item).
struct FileTreeCtrl::Node : public wxClientData {
  explicit Node(const FileProvider::File &f) : file(f) {}
  FileProvider::File file;
  // For a folder: its entries are in the tree. Before that it has an empty
  // child, so that the tree shows it can be opened.
  bool loaded{false};
};

FileTreeCtrl::FileTreeCtrl(wxWindow *parent, const Account &account)
    : wxPanel(parent), m_account(account),
      m_provider(CreateFileProvider(account)) {
  m_tree =
      new wxDataViewTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxDV_NO_HEADER | wxDV_MULTIPLE | wxDV_ROW_LINES);
  dv::StylePageView(m_tree);

  // The names are not to be edited.
  m_tree->GetColumn(0)->GetRenderer()->SetMode(wxDATAVIEW_CELL_INERT);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_tree, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  m_tree->Bind(wxEVT_DATAVIEW_ITEM_EXPANDING, &FileTreeCtrl::OnExpanding, this);
  m_tree->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED,
               &FileTreeCtrl::OnSelectionChanged, this);
  m_tree->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &FileTreeCtrl::OnActivated, this);
  m_tree->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &FileTreeCtrl::OnContextMenu,
               this);
  m_typeHelper = std::make_unique<DataViewTypeHelper>(m_tree);
  m_listId = ++s_lastRequestId;
  EventNotifier::Get()->Bind(wxEVT_FILETREE_LISTED, &FileTreeCtrl::OnListed,
                             this);

  // Once the control is on the screen. A remote host waits for
  // ConnectAsync().
  if (!m_account.IsRemote()) {
    CallAfter(&FileTreeCtrl::Reload);
  }
}

FileTreeCtrl::~FileTreeCtrl() {
  EventNotifier::Get()->Unbind(wxEVT_FILETREE_LISTED, &FileTreeCtrl::OnListed,
                               this);
  // What is queued is not wanted any more: the thread is told to end, and ends
  // after the folder that it is listing now.
  {
    std::lock_guard<std::mutex> lock(m_jobMutex);
    m_jobs.clear();
    m_jobs.push_back(Job::MakeShutdownJob());
  }
  m_jobReady.notify_one();
  if (m_worker.joinable()) {
    m_worker.join();
  }
}

void FileTreeCtrl::EnqueueListing(const wxString &dir) {
  // Started when it is first needed: most trees never reveal a file.
  if (!m_worker.joinable()) {
    m_worker = std::thread(&FileTreeCtrl::ListWorker, this, m_provider);
  }
  {
    std::lock_guard<std::mutex> lock(m_jobMutex);
    // The same folder again: the old job goes, the new one is last.
    auto listDirJob = Job::MakeListDirJob(dir);
    std::erase_if(m_jobs, [&](const Job &job) { return job == listDirJob; });
    m_jobs.push_back(std::move(listDirJob));
  }
  m_jobReady.notify_one();
}

// The thread of the tree. It never touches the tree: it has its own share of
// the provider, and it sends each answer on the EventNotifier (see
// ConnectAsync()).
void FileTreeCtrl::ListWorker(std::shared_ptr<FileProvider> provider) {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(m_jobMutex);
      m_jobReady.wait(lock, [this] { return !m_jobs.empty(); });
      job = std::move(m_jobs.front());
      m_jobs.pop_front();
    }
    if (job.IsShutdown()) {
      return;
    }
    FileTreeConnectedEvent event{wxEVT_FILETREE_LISTED};
    event.SetRequestId(m_listId);
    event.SetDir(job.dir);
    event.SetFiles(provider->ListFiles(job.dir));
    EventNotifier::Get()->AddPendingEvent(event);
  }
}

int FileTreeCtrl::ConnectAsync() {
  const int requestId = ++s_lastRequestId;
  ShowTopText(_("Connecting..."));
  // The worker never touches the tree, which may be gone before the host
  // answers: it has its own share of the provider, and it sends the result on
  // the EventNotifier, which lives as long as the process.
  std::thread([requestId, provider = m_provider, dir = m_root] {
    FileTreeConnectedEvent event{wxEVT_FILETREE_CONNECTED};
    event.SetRequestId(requestId);
    event.SetDir(dir);
    const Status status = provider->Connect();
    event.SetStatus(status);
    if (status.ok()) {
      event.SetFiles(provider->ListFiles(
          dir.empty() ? provider->GetCurrentWorkingDir() : dir));
    }
    EventNotifier::Get()->AddPendingEvent(event);
  }).detach();
  return requestId;
}

bool FileTreeCtrl::FinishConnect(const FileTreeConnectedEvent &event) {
  if (!event.GetStatus().ok()) {
    ShowTopText(_("Could not connect"));
    return false;
  }
  Fill(event.GetDir(), event.GetFiles(), {});
  return true;
}

void FileTreeCtrl::ShowTopText(const wxString &text) {
  m_tree->DeleteAllItems();
  // No node: it is not a file, and it sends no event.
  m_tree->AppendItem(wxDataViewItem(), text);
}

void FileTreeCtrl::SetRoot(const wxString &dir) {
  m_root = dir;
  Load(dir, {});
}

void FileTreeCtrl::Reload() {
  std::set<wxString> expanded;
  if (m_tree->GetChildCount(wxDataViewItem()) > 0) {
    CollectExpanded(m_tree->GetNthChild(wxDataViewItem(), 0), &expanded);
  }
  Load(m_root, expanded);
}

bool FileTreeCtrl::SelectPath(const wxString &path) {
  if (m_tree->GetChildCount(wxDataViewItem()) == 0) {
    return false;
  }
  const wxString target = ForCompare(path);
  const wxDataViewItem top = m_tree->GetNthChild(wxDataViewItem(), 0);
  const Node *node = NodeOf(top);
  if (node == nullptr || !IsSameOrInside(ForCompare(node->file.path), target)) {
    return false;
  }
  m_revealTarget = target;
  Reveal();
  return true;
}

void FileTreeCtrl::Reveal() {
  if (m_tree->GetChildCount(wxDataViewItem()) == 0) {
    return;
  }
  // From the top down: open the folder, find the entry that holds the path.
  wxDataViewItem item = m_tree->GetNthChild(wxDataViewItem(), 0);
  const Node *node = NodeOf(item);
  while (node != nullptr && ForCompare(node->file.path) != m_revealTarget) {
    if (node->file.IsFolder() && !node->loaded) {
      if (m_account.IsRemote()) {
        // The provider of a remote host answers one request at a time, for the
        // UI thread: not for the thread of the tree too.
        m_revealTarget.clear();
        return;
      }
      // Not listed yet: that is the slow part (a disk, a WSL share), so the
      // thread of the tree does it; OnListed() goes on from there.
      EnqueueListing(node->file.path);
      return;
    }
    m_tree->Expand(item);
    wxDataViewItem next;
    const int count = m_tree->GetChildCount(item);
    for (int i = 0; i < count && !next.IsOk(); ++i) {
      const wxDataViewItem child = m_tree->GetNthChild(item, i);
      const Node *childNode = NodeOf(child);
      if (childNode != nullptr &&
          IsSameOrInside(ForCompare(childNode->file.path), m_revealTarget)) {
        next = child;
      }
    }
    if (!next.IsOk()) {
      m_revealTarget.clear(); // Not in the tree
      return;
    }
    item = next;
    node = NodeOf(item);
  }
  m_revealTarget.clear(); // Done: another listing has nothing to go to
  if (node == nullptr) {
    return;
  }
  // Only this entry is selected (the tree takes many), and it is in view.
  wxEventBlocker blocker(m_tree, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_tree->UnselectAll();
  m_tree->Select(item);
  m_tree->EnsureVisible(item);
}

wxDataViewItem FileTreeCtrl::FindOnWay(const wxString &dir) const {
  const wxString wanted = ForCompare(dir);
  wxDataViewItem item = m_tree->GetNthChild(wxDataViewItem(), 0);
  const Node *node = NodeOf(item);
  while (node != nullptr && ForCompare(node->file.path) != wanted) {
    if (!node->loaded) {
      return wxDataViewItem();
    }
    wxDataViewItem next;
    const int count = m_tree->GetChildCount(item);
    for (int i = 0; i < count && !next.IsOk(); ++i) {
      const wxDataViewItem child = m_tree->GetNthChild(item, i);
      const Node *childNode = NodeOf(child);
      if (childNode != nullptr &&
          IsSameOrInside(ForCompare(childNode->file.path), wanted)) {
        next = child;
      }
    }
    if (!next.IsOk()) {
      return wxDataViewItem();
    }
    item = next;
    node = NodeOf(item);
  }
  return node != nullptr ? item : wxDataViewItem();
}

void FileTreeCtrl::OnListed(FileTreeConnectedEvent &event) {
  event.Skip(); // The other trees look at it too
  if (event.GetRequestId() != m_listId) {
    return;
  }
  // The folder goes into the tree if it is still not listed (the user may have
  // opened it meanwhile)...
  const wxDataViewItem item = FindOnWay(event.GetDir());
  Node *node = NodeOf(item);
  if (node != nullptr && node->file.IsFolder() && !node->loaded) {
    node->loaded = true;
    m_tree->DeleteChildren(item); // The empty child
    auto files = event.GetFiles();
    SortFiles(&files);
    for (const auto &file : files) {
      AddNode(item, file);
    }
  }
  // ...and the way goes on to the file that is wanted now, which may be another
  // one than when this folder was asked for.
  if (!m_revealTarget.empty()) {
    Reveal();
  }
}

std::optional<FileProvider::File> FileTreeCtrl::GetSelection() const {
  if (const Node *node = NodeOf(m_tree->GetSelection())) {
    return node->file;
  }
  return std::nullopt;
}

void FileTreeCtrl::Load(const wxString &dir,
                        const std::set<wxString> &expanded) {
  // No folder given: the one the provider starts in.
  const wxString start = dir.empty() ? m_provider->GetCurrentWorkingDir() : dir;
  std::optional<wxBusyCursor> busy;
  if (m_account.IsRemote()) {
    busy.emplace(); // The host takes a while to answer
  }
  Fill(dir, m_provider->ListFiles(start), expanded);
}

void FileTreeCtrl::Fill(const wxString &dir,
                        std::vector<FileProvider::File> files,
                        const std::set<wxString> &expanded) {
  m_revealTarget.clear(); // A reveal that was waiting belongs to the old tree
  // A remote host tells which folder it starts in when it lists it, so the top
  // is known after the listing.
  m_root =
      NormalizeRoot(dir.empty() ? m_provider->GetCurrentWorkingDir() : dir);
  SortFiles(&files);

  m_tree->DeleteAllItems();
  FileProvider::File top;
  top.path = m_root;
  top.type = FileProvider::FileType::kDir;
  auto *topNode = new Node(top);
  topNode->loaded = true; // Its entries are added below
  // Only the name of the folder, with no host: a full path or a long host
  // name does not fit in a narrow view.
  const wxString label = FolderNameOf(m_root);
  const wxDataViewItem topItem = m_tree->AppendContainer(
      wxDataViewItem(), label, wxDataViewTreeCtrl::NO_IMAGE,
      wxDataViewTreeCtrl::NO_IMAGE, topNode);
  const auto &bitmaps = AppManager::Get().GetBitmaps();
  m_tree->SetItemIcon(topItem, bitmaps.GetByAlias("folder", false));
  m_tree->SetItemExpandedIcon(topItem, bitmaps.GetByAlias("folder", false));
  for (const auto &file : files) {
    AddNode(topItem, file);
  }
  m_tree->Expand(topItem);
  RestoreExpanded(topItem, expanded);
}

void FileTreeCtrl::LoadChildren(const wxDataViewItem &item) {
  Node *node = NodeOf(item);
  if (node == nullptr || !node->file.IsFolder() || node->loaded) {
    return;
  }
  node->loaded = true;
  m_tree->DeleteChildren(item); // The empty child
  std::optional<wxBusyCursor> busy;
  if (m_account.IsRemote()) {
    busy.emplace(); // The host takes a while to answer
  }
  auto files = m_provider->ListFiles(node->file.path);
  SortFiles(&files);
  for (const auto &file : files) {
    AddNode(item, file);
  }
}

void FileTreeCtrl::RefreshFolder(const wxDataViewItem &item) {
  Node *node = NodeOf(item);
  if (node == nullptr || !node->file.IsFolder()) {
    return;
  }
  // The open folders: `item` itself, when it is open, and the ones inside it.
  std::set<wxString> expanded;
  CollectExpanded(item, &expanded);
  m_tree->Collapse(item);
  m_tree->DeleteChildren(item);
  // The empty child, so that the tree shows it can be opened (OnExpanding()).
  m_tree->AppendItem(item, wxEmptyString);
  node->loaded = false;
  if (expanded.contains(node->file.path)) {
    // Listed again now, and opened as they were.
    LoadChildren(item);
    m_tree->Expand(item);
    RestoreExpanded(item, expanded);
  }
}

void FileTreeCtrl::AddNode(const wxDataViewItem &parent,
                           const FileProvider::File &file) {
  const wxString name = file.path.AfterLast(m_provider->GetPathSeparator());
  const auto &bitmaps = AppManager::Get().GetBitmaps();
  if (file.IsFolder()) {
    const wxDataViewItem item =
        m_tree->AppendContainer(parent, name, wxDataViewTreeCtrl::NO_IMAGE,
                                wxDataViewTreeCtrl::NO_IMAGE, new Node(file));
    m_tree->SetItemIcon(item, bitmaps.GetByAlias("folder", false));
    m_tree->SetItemExpandedIcon(item, bitmaps.GetByAlias("folder", false));
    // Listed when the folder is opened (OnExpanding())
    m_tree->AppendItem(item, wxEmptyString);
    return;
  }
  const wxDataViewItem item = m_tree->AppendItem(
      parent, name, wxDataViewTreeCtrl::NO_IMAGE, new Node(file));
  m_tree->SetItemIcon(
      item, bitmaps.GetFileIcon(file.path,
                                wxSystemSettings::GetAppearance().IsDark()));
}

void FileTreeCtrl::CollectExpanded(const wxDataViewItem &item,
                                   std::set<wxString> *expanded) const {
  const Node *node = NodeOf(item);
  if (node == nullptr || !node->file.IsFolder() || !node->loaded ||
      !m_tree->IsExpanded(item)) {
    return;
  }
  expanded->insert(node->file.path);
  const int count = m_tree->GetChildCount(item);
  for (int i = 0; i < count; ++i) {
    CollectExpanded(m_tree->GetNthChild(item, i), expanded);
  }
}

void FileTreeCtrl::RestoreExpanded(const wxDataViewItem &item,
                                   const std::set<wxString> &expanded) {
  const int count = m_tree->GetChildCount(item);
  for (int i = 0; i < count; ++i) {
    const wxDataViewItem child = m_tree->GetNthChild(item, i);
    const Node *node = NodeOf(child);
    if (node == nullptr || !node->file.IsFolder() ||
        !expanded.contains(node->file.path)) {
      continue;
    }
    LoadChildren(child);
    m_tree->Expand(child);
    RestoreExpanded(child, expanded);
  }
}

FileTreeCtrl::Node *FileTreeCtrl::NodeOf(const wxDataViewItem &item) const {
  if (!item.IsOk()) {
    return nullptr;
  }
  // The empty child of a folder that is not listed yet has no node.
  return dynamic_cast<Node *>(m_tree->GetItemData(item));
}

wxString FileTreeCtrl::FolderNameOf(const wxString &path) {
  // A remote path uses '/' on any platform, so split on both separators.
  wxString trimmed = path;
  while (trimmed.length() > 1 &&
         (trimmed.EndsWith("/") || trimmed.EndsWith("\\"))) {
    trimmed.RemoveLast();
  }
  const size_t sep = trimmed.find_last_of("/\\");
  const wxString name = sep == wxString::npos ? trimmed : trimmed.Mid(sep + 1);
  // The top of a file system ("/") has no name; show the path.
  return name.empty() ? path : name;
}

wxString FileTreeCtrl::NormalizeRoot(const wxString &dir) const {
  if (!m_account.IsWSL()) {
    return dir;
  }
  // The provider starts in the share of the distribution ("\\wsl$\distro\..."),
  // the entries are Linux paths.
  wxString path = dir;
  const wxString share = wxString::Format(R"(\\wsl$\%s)", m_account.m_name);
  if (path.StartsWith(share)) {
    path = path.Mid(share.length());
  }
  path.Replace("\\", "/");
  return path.empty() ? wxString("/") : path;
}

wxString FileTreeCtrl::ForCompare(const wxString &path) const {
  wxString result = path;
  result.Replace("\\", "/");
  while (result.length() > 1 && result.EndsWith("/")) {
    result.RemoveLast();
  }
#ifdef __WXMSW__
  if (m_account.IsLocalHost()) {
    result.MakeLower(); // A Windows path ignores the case
  }
#endif
  return result;
}

bool FileTreeCtrl::IsSameOrInside(const wxString &dir,
                                  const wxString &path) const {
  if (dir == path) {
    return true;
  }
  return path.StartsWith(dir.EndsWith("/") ? dir : dir + "/");
}

void FileTreeCtrl::SendFileEvent(wxEventType type, const wxDataViewItem &item) {
  FileEvent event(type, GetId());
  event.SetEventObject(this);
  event.SetAccount(m_account);
  if (const Node *node = NodeOf(item)) {
    event.SetPath(node->file.path);
    event.SetIsDir(node->file.IsFolder());
  }
  ProcessWindowEvent(event);
}

void FileTreeCtrl::OnExpanding(wxDataViewEvent &event) {
  LoadChildren(event.GetItem());
  event.Skip();
}

void FileTreeCtrl::OnSelectionChanged(wxDataViewEvent &event) {
  event.Skip();
  if (NodeOf(event.GetItem()) != nullptr) {
    SendFileEvent(wxEVT_FILETREE_SELECTED, event.GetItem());
  }
}

wxDataViewItemArray FileTreeCtrl::SelectedItems() const {
  wxDataViewItemArray items;
  m_tree->GetSelections(items);
  return items;
}

void FileTreeCtrl::OnActivated(wxDataViewEvent &event) {
  const wxDataViewItemArray selected = SelectedItems();
  if (selected.size() > 1 && selected.Index(event.GetItem()) != wxNOT_FOUND) {
    // Several entries: every selected file is opened (the folders stay as they
    // are).
    OpenItems(selected);
    return;
  }
  event.Skip(); // A folder opens or closes
  if (NodeOf(event.GetItem()) != nullptr) {
    SendFileEvent(wxEVT_FILETREE_ACTIVATED, event.GetItem());
  }
}

void FileTreeCtrl::OnContextMenu(wxDataViewEvent &event) {
  // On empty space the item is not ok: the event has no path.
  SendFileEvent(wxEVT_FILETREE_MENU, event.GetItem());
  const wxDataViewItem item = event.GetItem();
  const Node *node = NodeOf(item);
  if (node == nullptr) {
    return;
  }
  if (const wxDataViewItemArray selected = SelectedItems();
      selected.size() > 1 && selected.Index(item) != wxNOT_FOUND) {
    ShowMultiSelectionMenu(selected);
    return;
  }
  const wxString path = node->file.path;
  const bool isFolder = node->file.IsFolder();
  // The top of the tree is not deleted from here.
  const bool isTop = !m_tree->GetItemParent(item).IsOk();

  wxMenu menu;
  const auto separate = [&menu] {
    if (menu.GetMenuItemCount() > 0) {
      menu.AppendSeparator();
    }
  };
  // The commands change the items of the tree: they run after the menu, not
  // while the tree handles the event.
  const auto bind = [this, &menu](int id, auto fn) {
    menu.Bind(wxEVT_MENU, [this, fn](wxCommandEvent &) { CallAfter(fn); }, id);
  };
  if (isFolder) {
    menu.Append(kIdRefresh, _("Refresh"));
    bind(kIdRefresh, [this, item] { RefreshFolder(item); });
    if (!isTop) {
      // The host moves the tree and the git view (see SideView).
      menu.Append(kIdSetRoot, _("Set This Folder as Root"));
      menu.Bind(
          wxEVT_MENU,
          [this, path](wxCommandEvent &) {
            FileEvent event{wxEVT_FILETREE_SET_ROOT};
            event.SetEventObject(this);
            event.SetPath(path);
            event.SetIsDir(true);
            event.SetAccount(m_account);
            EventNotifier::Get()->AddPendingEvent(event);
          },
          kIdSetRoot);
    }
    separate();
    menu.Append(kIdNewFolder, _("New Folder..."));
    bind(kIdNewFolder, [this, item] { NewEntry(item, /*isFolder=*/true); });
    menu.Append(kIdNewFile, _("New File..."));
    bind(kIdNewFile, [this, item] { NewEntry(item, /*isFolder=*/false); });
  }
  // A remote file is on another machine: this computer cannot show or open
  // it. A WSL path is a Linux path: Windows needs the path of its share.
  const bool isLocal = !m_account.IsRemote();
  const wxString localPath =
      m_account.IsWSL() ? wsl::ToWindowsPath(m_account.m_name, path) : path;
  if (isLocal && !isFolder) {
    separate();
    menu.Append(kIdOpenDefault, _("Open with Default App"));
    menu.Bind(
        wxEVT_MENU,
        [localPath](wxCommandEvent &) {
          ::wxLaunchDefaultApplication(localPath);
        },
        kIdOpenDefault);
  }
  const auto appendCopyPath = [&menu, &separate, path] {
    separate();
    menu.Append(kIdCopyPath, _("Copy Path"));
    menu.Bind(
        wxEVT_MENU, [path](wxCommandEvent &) { CopyText(path); }, kIdCopyPath);
  };
  // Delete is never the first entry (nor the last when it can be helped), so
  // that a click near the edge of the menu does not hit it.
  const bool copyPathFirst = menu.GetMenuItemCount() == 0;
  if (copyPathFirst) {
    appendCopyPath();
  }
  if (!isTop) {
    separate();
    menu.Append(kIdDelete, _("Delete..."));
    bind(kIdDelete, [this, item] { DeleteEntry(item); });
  }
  if (isLocal) {
    separate();
    menu.Append(kIdShowInFileManager, ShowInFileManagerLabel());
    menu.Bind(
        wxEVT_MENU,
        [localPath](wxCommandEvent &) { ShowInFileManager(localPath); },
        kIdShowInFileManager);
  }
  if (!copyPathFirst) {
    appendCopyPath();
  }
  m_tree->PopupMenu(&menu);
}

void FileTreeCtrl::NewEntry(const wxDataViewItem &folder, bool isFolder) {
  const Node *node = NodeOf(folder);
  if (node == nullptr || !node->file.IsFolder()) {
    return;
  }
  wxString name = ::wxGetTextFromUser(
      isFolder ? _("Name of the new folder:") : _("Name of the new file:"),
      isFolder ? _("New Folder") : _("New File"), wxEmptyString, this);
  name.Trim().Trim(false);
  if (name.empty()) {
    return; // Cancelled
  }
  if (!IsValidEntryName(name)) {
    ::wxMessageBox(wxString::Format(_("'%s' is not a valid name"), name),
                   "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }
  const wxChar sep = m_provider->GetPathSeparator();
  const wxString &dir = node->file.path;
  const wxString path = dir.EndsWith(sep) ? dir + name : dir + sep + name;

  Status st;
  {
    std::optional<wxBusyCursor> busy;
    if (m_account.IsRemote()) {
      busy.emplace(); // The host takes a while to answer
    }
    st = isFolder ? m_provider->MakeFolder(path) : m_provider->MakeFile(path);
  }
  if (!st.ok()) {
    KLOG_WARN() << "Could not create " << path << ": " << st.message();
    ::wxMessageBox(st.message(), "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }
  RefreshFolder(folder);
  // Opens the folder (it is listed with the new entry) and selects the entry.
  SelectPath(path);
}

void FileTreeCtrl::OpenItems(const wxDataViewItemArray &items) {
  for (const wxDataViewItem &item : items) {
    if (const Node *node = NodeOf(item); node && !node->file.IsFolder()) {
      SendFileEvent(wxEVT_FILETREE_ACTIVATED, item);
    }
  }
}

void FileTreeCtrl::ShowMultiSelectionMenu(const wxDataViewItemArray &items) {
  bool hasFile = false;
  bool hasDeletable = false;
  for (const wxDataViewItem &item : items) {
    if (const Node *node = NodeOf(item)) {
      hasFile |= !node->file.IsFolder();
      hasDeletable |= m_tree->GetItemParent(item).IsOk(); // Not the top
    }
  }
  wxMenu menu;
  // The commands change the items of the tree: they run after the menu.
  if (hasFile) {
    menu.Append(kIdOpen, _("Open"));
    menu.Bind(
        wxEVT_MENU,
        [this, items](wxCommandEvent &) {
          CallAfter([this, items] { OpenItems(items); });
        },
        kIdOpen);
  }
  if (hasDeletable) {
    if (hasFile) {
      menu.AppendSeparator();
    }
    menu.Append(kIdDelete, _("Delete..."));
    menu.Bind(
        wxEVT_MENU,
        [this, items](wxCommandEvent &) {
          CallAfter([this, items] { DeleteEntries(items); });
        },
        kIdDelete);
  }
  if (menu.GetMenuItemCount() > 0) {
    m_tree->PopupMenu(&menu);
  }
}

void FileTreeCtrl::DeleteEntries(const wxDataViewItemArray &items) {
  struct Target {
    wxString path;
    bool isFolder;
    wxDataViewItem parent;
    wxString compare;
  };
  std::vector<Target> targets;
  for (const wxDataViewItem &item : items) {
    const Node *node = NodeOf(item);
    const wxDataViewItem parent = m_tree->GetItemParent(item);
    if (node == nullptr || !parent.IsOk()) {
      continue; // The top is not deleted from here
    }
    targets.push_back({node->file.path, node->file.IsFolder(), parent,
                       ForCompare(node->file.path)});
  }
  // An entry inside a selected folder goes with the folder.
  std::erase_if(targets, [&targets](const Target &t) {
    return std::any_of(targets.begin(), targets.end(), [&t](const Target &o) {
      return &o != &t && o.isFolder &&
             t.compare.length() > o.compare.length() &&
             t.compare.StartsWith(o.compare.EndsWith("/") ? o.compare
                                                          : o.compare + "/");
    });
  });
  if (targets.empty()) {
    return;
  }
  if (targets.size() == 1) {
    // Same as a single entry
    for (const wxDataViewItem &item : items) {
      if (const Node *node = NodeOf(item);
          node && node->file.path == targets[0].path) {
        DeleteEntry(item);
        return;
      }
    }
    return;
  }
  const wxString question = wxString::Format(
      _("Delete these %d entries (folders with everything in them)?"),
      static_cast<int>(targets.size()));
  if (::wxMessageBox(question, "Kennel",
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
    return;
  }
  wxString errors;
  {
    std::optional<wxBusyCursor> busy;
    if (m_account.IsRemote()) {
      busy.emplace(); // The host takes a while to answer
    }
    for (const Target &t : targets) {
      if (Status st = m_provider->Remove(t.path); !st.ok()) {
        KLOG_WARN() << "Could not delete " << t.path << ": " << st.message();
        errors << t.path << ": " << st.message() << "\n";
      }
    }
  }
  if (!errors.empty()) {
    ::wxMessageBox(errors, "Kennel", wxOK | wxICON_ERROR, this);
  }
  // Also after an error: a part of a folder may be gone. The deepest folders
  // first: listing a folder makes the items of the folders in it go away.
  std::vector<std::pair<wxString, wxDataViewItem>> parents;
  for (const Target &t : targets) {
    const wxString key = ForCompare(NodeOf(t.parent)->file.path);
    if (std::none_of(parents.begin(), parents.end(),
                     [&key](const auto &p) { return p.first == key; })) {
      parents.emplace_back(key, t.parent);
    }
  }
  std::sort(parents.begin(), parents.end(), [](const auto &a, const auto &b) {
    return a.first.length() > b.first.length();
  });
  for (const auto &p : parents) {
    RefreshFolder(p.second);
  }
}

void FileTreeCtrl::DeleteEntry(const wxDataViewItem &item) {
  const Node *node = NodeOf(item);
  const wxDataViewItem parent = m_tree->GetItemParent(item);
  if (node == nullptr || !parent.IsOk()) {
    return;
  }
  // Copied: the node goes away with the item.
  const wxString path = node->file.path;
  const wxString question =
      node->file.IsFolder()
          ? wxString::Format(_("Delete the folder '%s' and everything in it?"),
                             path)
          : wxString::Format(_("Delete the file '%s'?"), path);
  if (::wxMessageBox(question, "Kennel",
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
    return;
  }
  Status st;
  {
    std::optional<wxBusyCursor> busy;
    if (m_account.IsRemote()) {
      busy.emplace(); // The host takes a while to answer
    }
    st = m_provider->Remove(path);
  }
  if (!st.ok()) {
    KLOG_WARN() << "Could not delete " << path << ": " << st.message();
    ::wxMessageBox(st.message(), "Kennel", wxOK | wxICON_ERROR, this);
  }
  // Also after an error: a part of a folder may be gone.
  RefreshFolder(parent);
}
