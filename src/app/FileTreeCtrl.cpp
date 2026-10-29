#include "app/FileTreeCtrl.hpp"

#include "PageViewEvent.hpp"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"

#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <algorithm>
#include <thread>

wxDEFINE_EVENT(wxEVT_FILETREE_SELECTED, FileEvent);
wxDEFINE_EVENT(wxEVT_FILETREE_ACTIVATED, FileEvent);
wxDEFINE_EVENT(wxEVT_FILETREE_MENU, FileEvent);
wxDEFINE_EVENT(wxEVT_FILETREE_CONNECTED, FileTreeConnectedEvent);

namespace {
// The id of the last request of ConnectAsync(). Only the UI thread uses it.
int s_lastRequestId = 0;

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
                             wxDV_NO_HEADER | wxDV_SINGLE | wxDV_ROW_LINES);
  StylePageView(m_tree);

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

  // Once the control is on the screen. A remote host waits for
  // ConnectAsync().
  if (!m_account.IsRemote()) {
    CallAfter(&FileTreeCtrl::Reload);
  }
}

FileTreeCtrl::~FileTreeCtrl() = default;

int FileTreeCtrl::ConnectAsync() {
  const int requestId = ++s_lastRequestId;
  ShowTopText(_("Connecting..."));
  // The worker never touches the tree, which may be gone before the host
  // answers: it has its own share of the provider, and it sends the result on
  // the EventNotifier, which lives as long as the process.
  std::thread([requestId, provider = m_provider, dir = m_root] {
    auto *event = new FileTreeConnectedEvent(wxEVT_FILETREE_CONNECTED);
    event->SetRequestId(requestId);
    event->SetDir(dir);
    const Status status = provider->Connect();
    event->SetStatus(status);
    if (status.ok()) {
      event->SetFiles(provider->ListFiles(
          dir.empty() ? provider->GetCurrentWorkingDir() : dir));
    }
    wxQueueEvent(EventNotifier::Get(), event); // Safe from any thread
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
  wxDataViewItem item = m_tree->GetNthChild(wxDataViewItem(), 0);
  const Node *node = NodeOf(item);
  if (node == nullptr || !IsSameOrInside(ForCompare(node->file.path), target)) {
    return false;
  }
  // From the top down: open the folder, find the entry that holds the path.
  while (ForCompare(node->file.path) != target) {
    LoadChildren(item);
    m_tree->Expand(item);
    wxDataViewItem next;
    const int count = m_tree->GetChildCount(item);
    for (int i = 0; i < count && !next.IsOk(); ++i) {
      const wxDataViewItem child = m_tree->GetNthChild(item, i);
      const Node *childNode = NodeOf(child);
      if (childNode != nullptr &&
          IsSameOrInside(ForCompare(childNode->file.path), target)) {
        next = child;
      }
    }
    if (!next.IsOk()) {
      return false;
    }
    item = next;
    node = NodeOf(item);
  }
  wxEventBlocker blocker(m_tree, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_tree->Select(item);
  m_tree->EnsureVisible(item);
  return true;
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

void FileTreeCtrl::OnActivated(wxDataViewEvent &event) {
  event.Skip(); // A folder opens or closes
  if (NodeOf(event.GetItem()) != nullptr) {
    SendFileEvent(wxEVT_FILETREE_ACTIVATED, event.GetItem());
  }
}

void FileTreeCtrl::OnContextMenu(wxDataViewEvent &event) {
  // On empty space the item is not ok: the event has no path.
  SendFileEvent(wxEVT_FILETREE_MENU, event.GetItem());
}
