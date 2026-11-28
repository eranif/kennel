#include "app/FileTreeCtrl.hpp"

#include "app/DataViewTypeHelper.h"
#include "app/DataViewUtils.hpp"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/FileTreeChangedEvent.hpp"
#include "core/FileTypesEvent.hpp"
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
constexpr int kTreeIndent = 16;

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
      m_provider(CreateFileProvider(account)), m_uid(++s_lastRequestId) {
  CreateTree();
  BindEvents();
  // Once the control is on the screen. A remote host waits for
  // ConnectAsync().
  if (!m_account.IsRemote()) {
    CallAfter(&FileTreeCtrl::Reload);
  }
}

void FileTreeCtrl::CreateTree() {
  long treeStyle = wxDV_NO_HEADER | wxDV_MULTIPLE;
#ifdef __WXMAC__
  treeStyle |= wxDV_ROW_LINES;
#endif
  m_tree = new wxDataViewTreeCtrl(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, treeStyle);
  dv::StylePageView(m_tree);
  m_tree->SetIndent(m_tree->FromDIP(kTreeIndent));
  // The names are not to be edited.
  m_tree->GetColumn(0)->GetRenderer()->SetMode(wxDATAVIEW_CELL_INERT);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_tree, wxSizerFlags(1).Expand());
  SetSizer(sizer);
  m_typeHelper = std::make_unique<DataViewTypeHelper>(m_tree);
}

void FileTreeCtrl::BindEvents() {
  m_tree->Bind(wxEVT_DATAVIEW_ITEM_EXPANDING, &FileTreeCtrl::OnExpanding, this);
  m_tree->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED,
               &FileTreeCtrl::OnSelectionChanged, this);
  m_tree->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &FileTreeCtrl::OnActivated, this);
  m_tree->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &FileTreeCtrl::OnContextMenu,
               this);
  auto *notifier = EventNotifier::Get();
  notifier->Bind(wxEVT_FILETREE_LISTED, &FileTreeCtrl::OnListed, this);
  notifier->Bind(wxEVT_FILETREE_CHANGED, &FileTreeCtrl::OnChanged, this);
  notifier->Bind(wxEVT_APP_PREFS_CHANGED, &FileTreeCtrl::OnPrefsChanged, this);
}

FileTreeCtrl::~FileTreeCtrl() {
  EventNotifier::Get()->Unbind(wxEVT_FILETREE_LISTED, &FileTreeCtrl::OnListed,
                               this);
  EventNotifier::Get()->Unbind(wxEVT_FILETREE_CHANGED, &FileTreeCtrl::OnChanged,
                               this);
  EventNotifier::Get()->Unbind(wxEVT_APP_PREFS_CHANGED,
                               &FileTreeCtrl::OnPrefsChanged, this);
  // What is queued is not wanted any more: the thread is told to end, and ends
  // after the job that it is doing now.
  m_stopping = true;
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

//===------------------------
// The jobs of the thread of the tree
//===------------------------

FileTreeCtrl::Job FileTreeCtrl::Job::MakeShutdownJob() {
  Job job;
  job.type = JobType::Shutdown;
  return job;
}

FileTreeCtrl::Job FileTreeCtrl::Job::MakeConnectJob(int requestId,
                                                    const wxString &dir) {
  Job job;
  job.type = JobType::Connect;
  job.requestId = requestId;
  job.dir = dir;
  return job;
}

FileTreeCtrl::Job
FileTreeCtrl::Job::MakeListJob(JobType type, const wxString &dir,
                               const std::set<wxString> &expanded) {
  Job job;
  job.type = type;
  job.dir = dir;
  job.expanded = expanded;
  return job;
}

FileTreeCtrl::Job
FileTreeCtrl::Job::MakeReadFirstLinesJob(const std::vector<wxString> &paths) {
  Job job;
  job.type = JobType::ReadFirstLines;
  job.paths = paths;
  return job;
}

FileTreeCtrl::Job FileTreeCtrl::Job::MakeCreateJob(JobType type,
                                                   const wxString &path,
                                                   const wxString &parentDir) {
  Job job;
  job.type = type;
  job.paths = {path};
  job.refreshDirs = {parentDir};
  job.selectPath = path;
  return job;
}

FileTreeCtrl::Job
FileTreeCtrl::Job::MakeRemoveJob(const std::vector<wxString> &paths,
                                 const std::vector<wxString> &refreshDirs) {
  Job job;
  job.type = JobType::Remove;
  job.paths = paths;
  job.refreshDirs = refreshDirs;
  return job;
}

bool FileTreeCtrl::Job::IsSameListing(const Job &other) const {
  return other.type == type && other.dir == dir && other.expanded == expanded;
}

Status FileTreeCtrl::Job::ApplyTo(FileProvider &provider,
                                  const wxString &path) const {
  switch (type) {
  case JobType::MakeFolder:
    return provider.MakeFolder(path);
  case JobType::MakeFile:
    return provider.MakeFile(path);
  case JobType::Remove:
    return provider.Remove(path);
  case JobType::Connect:
  case JobType::ListRoot:
  case JobType::ListDir:
  case JobType::ReadFirstLines:
  case JobType::Shutdown:
    break;
  }
  return Status::Error(_("This job does not change files"));
}

void FileTreeCtrl::EnsureWorker() {
  // Started when it is first needed.
  if (!m_worker.joinable()) {
    m_worker = std::thread(&FileTreeCtrl::ListWorker, this, m_provider);
  }
}

void FileTreeCtrl::PushJob(Job job) {
  EnsureWorker();
  {
    std::lock_guard<std::mutex> lock(m_jobMutex);
    m_jobs.push_back(std::move(job));
  }
  m_jobReady.notify_one();
}

void FileTreeCtrl::EnqueueListing(const wxString &dir,
                                  const std::set<wxString> &expanded) {
  EnsureWorker();
  {
    std::lock_guard<std::mutex> lock(m_jobMutex);
    // The same folder again: the old job goes, the new one is last.
    Job job = Job::MakeListJob(JobType::ListDir, dir, expanded);
    std::erase_if(m_jobs, [&job](const Job &queued) {
      return queued.IsSameListing(job);
    });
    m_jobs.push_back(std::move(job));
  }
  m_jobReady.notify_one();
}

// The thread of the tree. It never touches the tree: it has its own share of
// the provider, and it sends each answer on the EventNotifier.
void FileTreeCtrl::ListWorker(std::shared_ptr<FileProvider> provider) {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(m_jobMutex);
      m_jobReady.wait(lock, [this] { return !m_jobs.empty(); });
      job = std::move(m_jobs.front());
      m_jobs.pop_front();
    }
    if (job.type == JobType::Shutdown) {
      return;
    }
    RunJob(*provider, job);
  }
}

void FileTreeCtrl::RunJob(FileProvider &provider, const Job &job) {
  switch (job.type) {
  case JobType::Connect:
    return RunConnectJob(provider, job);
  case JobType::ListRoot:
  case JobType::ListDir:
    return RunListJob(provider, job);
  case JobType::ReadFirstLines:
    return RunReadFirstLinesJob(provider, job);
  case JobType::MakeFolder:
  case JobType::MakeFile:
  case JobType::Remove:
    return RunChangeJob(provider, job);
  case JobType::Shutdown:
    return;
  }
}

void FileTreeCtrl::ListInto(FileProvider &provider, const wxString &dir,
                            FileTreeConnectedEvent *event) {
  // No folder given: the one the provider starts in. A remote host tells which
  // one that is when it lists it.
  event->SetFiles(
      provider.ListFiles(dir.empty() ? provider.GetCurrentWorkingDir() : dir));
  event->SetDir(dir.empty() ? provider.GetCurrentWorkingDir() : dir);
}

void FileTreeCtrl::RunConnectJob(FileProvider &provider, const Job &job) {
  FileTreeConnectedEvent event{wxEVT_FILETREE_CONNECTED};
  event.SetRequestId(job.requestId);
  event.SetStatus(provider.Connect());
  if (event.GetStatus().ok()) {
    ListInto(provider, job.dir, &event);
  }
  EventNotifier::Get()->AddPendingEvent(event);
}

void FileTreeCtrl::RunListJob(FileProvider &provider, const Job &job) {
  FileTreeConnectedEvent event{wxEVT_FILETREE_LISTED};
  event.SetRequestId(m_uid);
  event.SetRoot(job.type == JobType::ListRoot);
  event.SetExpanded(job.expanded);
  ListInto(provider, job.dir, &event);
  EventNotifier::Get()->AddPendingEvent(event);
}

void FileTreeCtrl::RunReadFirstLinesJob(FileProvider &provider,
                                        const Job &job) {
  const auto lines = provider.ReadFirstLines(job.paths, &m_stopping);
  if (lines.empty() || m_stopping) {
    return;
  }
  FileTypesEvent event{wxEVT_FILETREE_TYPES_RESOLVED};
  event.SetTreeUID(m_uid);
  event.SetLines(lines);
  EventNotifier::Get()->AddPendingEvent(event);
}

void FileTreeCtrl::RunChangeJob(FileProvider &provider, const Job &job) {
  wxString errors;
  for (const wxString &path : job.paths) {
    const Status st = job.ApplyTo(provider, path);
    if (!st.ok()) {
      KLOG_WARN() << "Could not change " << path << ": " << st.message();
      errors << path << ": " << st.message() << "\n";
    }
  }
  FileTreeChangedEvent event{wxEVT_FILETREE_CHANGED};
  event.SetTreeUID(m_uid);
  event.SetError(errors);
  event.SetRefreshDirs(job.refreshDirs);
  event.SetSelectPath(errors.empty() ? job.selectPath : wxString());
  EventNotifier::Get()->AddPendingEvent(event);
}

//===------------------------
// Asking for the files
//===------------------------

int FileTreeCtrl::ConnectAsync() {
  const int requestId = ++s_lastRequestId;
  ShowTopText(_("Connecting..."));
  PushJob(Job::MakeConnectJob(requestId, m_root));
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
  LoadRoot(dir, {});
}

void FileTreeCtrl::Reload() {
  std::set<wxString> expanded;
  if (m_tree->GetChildCount(wxDataViewItem()) > 0) {
    CollectExpanded(m_tree->GetNthChild(wxDataViewItem(), 0), &expanded);
  }
  LoadRoot(m_root, expanded);
}

void FileTreeCtrl::LoadRoot(const wxString &dir,
                            const std::set<wxString> &expanded) {
  PushJob(Job::MakeListJob(JobType::ListRoot, dir, expanded));
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
  wxDataViewItem item = m_tree->GetNthChild(wxDataViewItem(), 0);
  while (item.IsOk() && !IsRevealTarget(item)) {
    const Node *node = NodeOf(item);
    if (node != nullptr && node->file.IsFolder() && !node->loaded) {
      // Not listed yet: the thread of the tree does it, and OnListed() goes on
      // from there.
      EnqueueListing(node->file.path, {});
      return;
    }
    m_tree->Expand(item);
    item = FindChildOnWay(item, m_revealTarget);
  }
  m_revealTarget.clear(); // Done: another listing has nothing to go to
  if (item.IsOk()) {
    SelectOnly(item);
  }
}

bool FileTreeCtrl::IsRevealTarget(const wxDataViewItem &item) const {
  const Node *node = NodeOf(item);
  return node != nullptr && ForCompare(node->file.path) == m_revealTarget;
}

void FileTreeCtrl::SelectOnly(const wxDataViewItem &item) {
  // Only this entry is selected (the tree takes many), and it is in view.
  wxEventBlocker blocker(m_tree, wxEVT_DATAVIEW_SELECTION_CHANGED);
  m_tree->UnselectAll();
  m_tree->Select(item);
  m_tree->EnsureVisible(item);
}

wxDataViewItem FileTreeCtrl::FindChildOnWay(const wxDataViewItem &item,
                                            const wxString &wanted) const {
  const int count = m_tree->GetChildCount(item);
  for (int i = 0; i < count; ++i) {
    const wxDataViewItem child = m_tree->GetNthChild(item, i);
    const Node *node = NodeOf(child);
    if (node != nullptr &&
        IsSameOrInside(ForCompare(node->file.path), wanted)) {
      return child;
    }
  }
  return wxDataViewItem();
}

wxDataViewItem FileTreeCtrl::FindChildByPath(const wxDataViewItem &folder,
                                             const wxString &path) const {
  const int count = m_tree->GetChildCount(folder);
  for (int i = 0; i < count; ++i) {
    const wxDataViewItem child = m_tree->GetNthChild(folder, i);
    const Node *node = NodeOf(child);
    if (node != nullptr && node->file.path == path) {
      return child;
    }
  }
  return wxDataViewItem();
}

wxDataViewItem FileTreeCtrl::FindOnWay(const wxString &dir) const {
  const wxString wanted = ForCompare(dir);
  wxDataViewItem item = m_tree->GetNthChild(wxDataViewItem(), 0);
  while (item.IsOk()) {
    const Node *node = NodeOf(item);
    if (node == nullptr) {
      break;
    }
    if (ForCompare(node->file.path) == wanted) {
      return item;
    }
    if (!node->loaded) {
      break;
    }
    item = FindChildOnWay(item, wanted);
  }
  return wxDataViewItem();
}

void FileTreeCtrl::OnPrefsChanged(wxCommandEvent &event) {
  event.Skip();
  dv::StylePageView(m_tree);
  dv::AutoSizeColumns(m_tree);
}

void FileTreeCtrl::OnListed(FileTreeConnectedEvent &event) {
  event.Skip(); // The other trees look at it too
  if (event.GetRequestId() != m_uid) {
    return;
  }
  if (event.IsRoot()) {
    Fill(event.GetDir(), event.GetFiles(), event.GetExpanded());
  } else {
    FillFolder(event);
  }
}

void FileTreeCtrl::OnChanged(FileTreeChangedEvent &event) {
  event.Skip(); // The other trees look at it too
  if (event.GetTreeUID() != m_uid) {
    return;
  }
  if (!event.GetError().empty()) {
    ::wxMessageBox(event.GetError(), "Kennel", wxOK | wxICON_ERROR, this);
  }
  // The outer folders first: they ask for the folders in them that are open.
  for (const wxString &dir : event.GetRefreshDirs()) {
    RefreshFolder(FindOnWay(dir));
  }
  if (!event.GetSelectPath().empty()) {
    SelectPath(event.GetSelectPath());
  }
}

void FileTreeCtrl::Fill(const wxString &dir,
                        std::vector<FileProvider::File> files,
                        const std::set<wxString> &expanded) {
  m_revealTarget.clear(); // A reveal that was waiting belongs to the old tree
  m_root = NormalizeRoot(dir);
  const wxDataViewItem top = AddTop();
  AddNodes(top, std::move(files), expanded);
  m_tree->Expand(top);
}

wxDataViewItem FileTreeCtrl::AddTop() {
  m_tree->DeleteAllItems();
  FileProvider::File top;
  top.path = m_root;
  top.type = FileProvider::FileType::kDir;
  auto *topNode = new Node(top);
  topNode->loaded = true; // Its entries are added by the caller
  // Only the name of the folder, with no host: a full path or a long host
  // name does not fit in a narrow view.
  const wxDataViewItem item = m_tree->AppendContainer(
      wxDataViewItem(), FolderNameOf(m_root), wxDataViewTreeCtrl::NO_IMAGE,
      wxDataViewTreeCtrl::NO_IMAGE, topNode);
  const auto &folder =
      AppManager::Get().GetBitmaps().GetByAlias("folder", false);
  m_tree->SetItemIcon(item, folder);
  m_tree->SetItemExpandedIcon(item, folder);
  return item;
}

void FileTreeCtrl::AddNodes(const wxDataViewItem &parent,
                            std::vector<FileProvider::File> files,
                            const std::set<wxString> &expanded) {
  SortFiles(&files);
  for (const auto &file : files) {
    AddNode(parent, file);
  }
  RequestFirstLines(files);
  // The folders that were open: listed now, and opened when they are there.
  for (const auto &file : files) {
    if (file.IsFolder() && expanded.contains(file.path)) {
      EnqueueListing(file.path, expanded);
    }
  }
}

void FileTreeCtrl::FillFolder(const FileTreeConnectedEvent &event) {
  m_busy.reset(); // The listing is here
  // The folder goes into the tree if it is still not listed (the user may have
  // opened it meanwhile)...
  const wxDataViewItem item = FindOnWay(event.GetDir());
  Node *node = NodeOf(item);
  if (node != nullptr && node->file.IsFolder() && !node->loaded) {
    node->loaded = true;
    m_tree->DeleteChildren(item); // The empty child
    AddNodes(item, event.GetFiles(), event.GetExpanded());
    // The folder was asked to open before it was listed (OnExpanding() said
    // no): it opens now.
    m_tree->Expand(item);
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

bool FileTreeCtrl::LoadChildrenIfFirstTime(const wxDataViewItem &item) {
  const Node *node = NodeOf(item);
  if (node == nullptr || !node->file.IsFolder() || node->loaded) {
    return false;
  }
  EnqueueListing(node->file.path, {});
  return true;
}

void FileTreeCtrl::RequestFirstLines(
    const std::vector<FileProvider::File> &files) {
  std::vector<wxString> paths;
  for (const auto &file : files) {
    const wxString name = file.path.AfterLast(m_provider->GetPathSeparator());
    // No extension, and not empty
    if (file.IsFile() && file.size != 0 && !name.Contains(".")) {
      paths.push_back(file.path);
    }
  }
  if (!paths.empty()) {
    PushJob(Job::MakeReadFirstLinesJob(paths));
  }
}

void FileTreeCtrl::ApplyFirstLines(const std::map<wxString, wxString> &lines) {
  for (const auto &[path, line] : lines) {
    ApplyFirstLine(path, line);
  }
}

void FileTreeCtrl::ApplyFirstLine(const wxString &path, const wxString &line) {
  // The folder of the file, when it is in the tree; then the file in it.
  const wxDataViewItem folder =
      FindOnWay(path.BeforeLast(m_provider->GetPathSeparator()));
  const wxDataViewItem item = FindChildByPath(folder, path);
  Node *node = NodeOf(item);
  if (node != nullptr && node->file.IsFile()) {
    node->file.firstLine = line;
    SetFileIcon(item, node->file);
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
    // Listed now, and opened as it was (see FillFolder()).
    EnqueueListing(node->file.path, expanded);
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
  SetFileIcon(item, file);
}

void FileTreeCtrl::SetFileIcon(const wxDataViewItem &item,
                               const FileProvider::File &file) {
  m_tree->SetItemIcon(item,
                      AppManager::Get().GetBitmaps().GetFileIcon(
                          file.path, wxSystemSettings::GetAppearance().IsDark(),
                          file.firstLine));
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
  if (LoadChildrenIfFirstTime(event.GetItem())) {
    m_busy.emplace();
    event.Veto(); // It opens when the listing is there (FillFolder())
    return;
  }
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
  const wxString name = AskEntryName(isFolder);
  if (name.empty()) {
    return;
  }
  // The answer (see OnChanged()) lists the folder again with the new entry
  // selected.
  const JobType type = isFolder ? JobType::MakeFolder : JobType::MakeFile;
  const wxString &dir = node->file.path;
  PushJob(Job::MakeCreateJob(type, JoinPath(dir, name), dir));
}

wxString FileTreeCtrl::AskEntryName(bool isFolder) {
  wxString name = ::wxGetTextFromUser(
      isFolder ? _("Name of the new folder:") : _("Name of the new file:"),
      isFolder ? _("New Folder") : _("New File"), wxEmptyString, this);
  name.Trim().Trim(false);
  if (!name.empty() && !IsValidEntryName(name)) {
    ::wxMessageBox(wxString::Format(_("'%s' is not a valid name"), name),
                   "Kennel", wxOK | wxICON_ERROR, this);
    return wxEmptyString;
  }
  return name;
}

wxString FileTreeCtrl::JoinPath(const wxString &dir,
                                const wxString &name) const {
  const wxChar sep = m_provider->GetPathSeparator();
  return dir.EndsWith(sep) ? dir + name : dir + sep + name;
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

bool FileTreeCtrl::DeleteTarget::IsInside(const DeleteTarget &folder) const {
  return &folder != this && folder.isFolder &&
         compare.length() > folder.compare.length() &&
         compare.StartsWith(folder.compare.EndsWith("/")
                                ? folder.compare
                                : folder.compare + "/");
}

void FileTreeCtrl::DeleteEntry(const wxDataViewItem &item) {
  wxDataViewItemArray items;
  items.Add(item);
  DeleteEntries(items);
}

void FileTreeCtrl::DeleteEntries(const wxDataViewItemArray &items) {
  const std::vector<DeleteTarget> targets = GetDeleteTargets(items);
  if (targets.empty() || !ConfirmDelete(targets)) {
    return;
  }
  std::vector<wxString> paths;
  for (const DeleteTarget &target : targets) {
    paths.push_back(target.path);
  }
  // The answer (see OnChanged()) lists the folders again, also after an error:
  // a part of a folder may be gone.
  PushJob(Job::MakeRemoveJob(paths, GetParentDirs(targets)));
}

std::vector<FileTreeCtrl::DeleteTarget>
FileTreeCtrl::GetDeleteTargets(const wxDataViewItemArray &items) {
  std::vector<DeleteTarget> targets;
  for (const wxDataViewItem &item : items) {
    const Node *node = NodeOf(item);
    const Node *parent = NodeOf(m_tree->GetItemParent(item));
    if (node == nullptr || parent == nullptr) {
      continue; // The top is not deleted from here
    }
    DeleteTarget target;
    target.path = node->file.path;
    target.parentDir = parent->file.path;
    target.compare = ForCompare(node->file.path);
    target.isFolder = node->file.IsFolder();
    targets.push_back(target);
  }
  RemoveNestedTargets(&targets);
  return targets;
}

void FileTreeCtrl::RemoveNestedTargets(
    std::vector<DeleteTarget> *targets) const {
  const std::vector<DeleteTarget> all = *targets;
  std::erase_if(*targets, [&all](const DeleteTarget &target) {
    return std::any_of(
        all.begin(), all.end(),
        [&target](const DeleteTarget &o) { return target.IsInside(o); });
  });
}

bool FileTreeCtrl::ConfirmDelete(const std::vector<DeleteTarget> &targets) {
  wxString question = wxString::Format(
      _("Delete these %d entries (folders with everything in them)?"),
      static_cast<int>(targets.size()));
  if (targets.size() == 1 && targets[0].isFolder) {
    question = wxString::Format(
        _("Delete the folder '%s' and everything in it?"), targets[0].path);
  } else if (targets.size() == 1) {
    question = wxString::Format(_("Delete the file '%s'?"), targets[0].path);
  }
  return ::wxMessageBox(question, "Kennel",
                        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                        this) == wxYES;
}

std::vector<wxString>
FileTreeCtrl::GetParentDirs(const std::vector<DeleteTarget> &targets) const {
  std::vector<wxString> dirs;
  for (const DeleteTarget &target : targets) {
    if (std::find(dirs.begin(), dirs.end(), target.parentDir) == dirs.end()) {
      dirs.push_back(target.parentDir);
    }
  }
  // The outer folders first: a folder that is listed again asks for the open
  // folders in it, so the ones inside it need no listing of their own.
  std::sort(dirs.begin(), dirs.end(), [](const wxString &a, const wxString &b) {
    return a.length() < b.length();
  });
  return dirs;
}
