#include "app/GitView.hpp"

#include "app/FileTreeCtrl.hpp"
#include "app/GitCommitMessageDlg.hpp"
#include "app/ToolBarUtils.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/GitDriverLocal.h"
#include "core/GitDriverSSH.h"
#include "core/GitDriverWSL.h"
#include "core/Logger.h"

#include <wx/app.h>
#include <wx/aui/auibar.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dataview.h>
#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/numdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <thread>
#include <wx/wupdlock.h>

namespace {
const int kIdRefresh = wxWindow::NewControlId();
const int kIdOpenFile = wxWindow::NewControlId();
const int kIdShowDiff = wxWindow::NewControlId();
const int kIdAdd = wxWindow::NewControlId();
const int kIdResetFile = wxWindow::NewControlId();
const int kIdBlame = wxWindow::NewControlId();
const int kIdFileLog = wxWindow::NewControlId();
const int kIdCommit = wxWindow::NewControlId();
const int kIdPush = wxWindow::NewControlId();
const int kIdRepoLog = wxWindow::NewControlId();
const int kIdResetRepo = wxWindow::NewControlId();

// How a status letter of git is shown: a word and the alias of an icon.
struct StatusLook {
  wxString word;
  const char *icon;
};

StatusLook LookOf(char letter) {
  switch (letter) {
  case 'M':
    return {_("Modified"), "git-modified"};
  case 'T':
    return {_("Type changed"), "git-modified"};
  case 'A':
    return {_("New"), "git-new"};
  case 'D':
    return {_("Deleted"), "git-deleted"};
  case 'R':
    return {_("Renamed"), "git-renamed"};
  case 'C':
    return {_("Copied"), "git-renamed"};
  default:
    return {_("Changed"), "git-modified"};
  }
}

// A cell with a text and an icon (the icon can be empty).
wxVariant IconText(const wxString &text,
                   const wxBitmapBundle &icon = wxBitmapBundle()) {
  wxVariant cell;
  cell << wxDataViewIconText(text, icon);
  return cell;
}

// The file `path` of the repository at `root` in the notation of `account`.
wxString FullPath(const Account &account, const wxString &root,
                  const wxString &path) {
  const wxString full = root + "/" + path;
  // The separators of this machine
  return account.IsLocalHost() ? wxFileName(full).GetFullPath() : full;
}

// Whether git can run for `account`: it needs a working folder (written with
// "~" it is not supported yet).
bool HasFolder(const Account &account) {
  return !account.m_rootDir.empty() && !account.m_rootDir.StartsWith("~");
}

// The driver for the files of `account`, which has a folder (see HasFolder()).
std::unique_ptr<GitDriver> MakeDriver(const Account &account) {
  const wxString &folder = account.m_rootDir;
  if (account.IsRemote()) {
    return std::make_unique<GitDriverSSH>(account.m_name, account.m_user,
                                          folder);
  }
  if (account.IsWSL()) {
    return std::make_unique<GitDriverWSL>(account.m_name, folder);
  }
  return std::make_unique<GitDriverLocal>(folder);
}

// `path` as one argument of a command line of any shell: in double quotes,
// with / for the separator (Windows takes it too).
wxString QuoteForShell(const wxString &path) {
  wxString quoted = path;
  quoted.Replace("\\", "/");
  return "\"" + quoted + "\"";
}
} // namespace

wxDEFINE_EVENT(wxEVT_GITVIEW_SHOW_DIFF, FileEvent);
wxDEFINE_EVENT(wxEVT_GITVIEW_SHOW_TEXT, FileEvent);
wxDEFINE_EVENT(wxEVT_GITVIEW_RUN_IN_TERMINAL, FileEvent);

GitView::GitView(wxWindow *parent) : wxPanel(parent) {
  m_toolbar = new wxAuiToolBar(this);
  toolbar::AddTool(m_toolbar, kIdRefresh, _("Refresh"),
                   AppManager::Get().GetBitmaps().GetByAlias("reload", false),
                   _("Refresh the git status"));
  const auto &bitmaps = AppManager::Get().GetBitmaps();
  m_toolbar->AddSeparator();
  toolbar::AddTool(m_toolbar, kIdCommit, _("Commit"),
                   bitmaps.GetByAlias("git-commit", false),
                   _("Commit the staged changes..."));
  toolbar::AddTool(m_toolbar, kIdPush, _("Push"),
                   bitmaps.GetByAlias("git-push", false), _("Push"));
  toolbar::AddTool(m_toolbar, kIdRepoLog, _("Log"),
                   bitmaps.GetByAlias("git-log", false),
                   _("Show the log of the repository"));
  toolbar::AddTool(m_toolbar, kIdResetRepo, _("Reset"),
                   bitmaps.GetByAlias("git-reset", false),
                   _("Reset the repository to HEAD..."));
  m_toolbar->Realize();
  toolbar::ApplyStyle(m_toolbar);

  m_branch = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_list =
      new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxDV_MULTIPLE | wxDV_ROW_LINES);
  StylePageView(m_list);
  m_list->AppendIconTextColumn(_("Status"), wxDATAVIEW_CELL_INERT, 130);
  m_list->AppendIconTextColumn(_("Path"), wxDATAVIEW_CELL_INERT, 300);

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_toolbar, wxSizerFlags(0).Expand());
  sizer->Add(m_branch, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(5)));
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  m_toolbar->Bind(wxEVT_TOOL, &GitView::OnRefresh, this, kIdRefresh);
  for (const int id : {kIdCommit, kIdPush, kIdRepoLog, kIdResetRepo}) {
    m_toolbar->Bind(wxEVT_TOOL, &GitView::OnMenuAction, this, id);
  }
  m_list->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &GitView::OnActivated, this);
  m_list->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &GitView::OnContextMenu, this);
  auto *notifier = EventNotifier::Get();
  notifier->Bind(wxEVT_SESSION_PAGE_ADDED, &GitView::OnSessionPageAdded, this);
  notifier->Bind(wxEVT_SESSION_PAGE_ACTIVITY, &GitView::OnSessionPageActivity,
                 this);
  notifier->Bind(wxEVT_SESSION_PAGE_REMOVED, &GitView::OnSessionPageRemoved,
                 this);
  notifier->Bind(wxEVT_SESSION_PAGE_RENAMED, &GitView::OnSessionRenamed, this);
  notifier->Bind(wxEVT_SESSION_PAGE_SHOWN, &GitView::OnSessionPageShown, this);
  notifier->Bind(wxEVT_SESSION_PAGES_CLEARED, &GitView::OnSessionPagesCleared,
                 this);
  notifier->Bind(wxEVT_GIT_STATUS, &GitView::OnGitStatus, this);
  notifier->Bind(wxEVT_GIT_DIFF, &GitView::OnGitDiff, this);
  notifier->Bind(wxEVT_GIT_COMMAND, &GitView::OnGitCommand, this);
  notifier->Bind(wxEVT_FILE_SAVED, &GitView::OnFileSaved, this);
  Refill();
}

// A driver ends by waiting for the git that runs, which can take seconds (an
// ssh host that does not answer). That is done on another thread: the UI goes
// on. The destructor of the view waits for these threads.
void GitView::DeleteInBackground(std::unique_ptr<GitDriver> driver) {
  if (driver) {
    m_reapers.emplace_back(
        [driver = std::move(driver)]() mutable { driver.reset(); });
  }
}

GitView::~GitView() {
  for (std::thread &reaper : m_reapers) {
    reaper.join();
  }
  auto *notifier = EventNotifier::Get();
  notifier->Unbind(wxEVT_SESSION_PAGE_ACTIVITY, &GitView::OnSessionPageActivity,
                   this);
  notifier->Unbind(wxEVT_SESSION_PAGE_ADDED, &GitView::OnSessionPageAdded,
                   this);
  notifier->Unbind(wxEVT_SESSION_PAGE_REMOVED, &GitView::OnSessionPageRemoved,
                   this);
  notifier->Unbind(wxEVT_SESSION_PAGE_RENAMED, &GitView::OnSessionRenamed,
                   this);
  notifier->Unbind(wxEVT_SESSION_PAGE_SHOWN, &GitView::OnSessionPageShown,
                   this);
  notifier->Unbind(wxEVT_SESSION_PAGES_CLEARED, &GitView::OnSessionPagesCleared,
                   this);
  notifier->Unbind(wxEVT_GIT_STATUS, &GitView::OnGitStatus, this);
  notifier->Unbind(wxEVT_GIT_DIFF, &GitView::OnGitDiff, this);
  notifier->Unbind(wxEVT_GIT_COMMAND, &GitView::OnGitCommand, this);
  notifier->Unbind(wxEVT_FILE_SAVED, &GitView::OnFileSaved, this);
}

void GitView::ApplyToolBarStyle() { toolbar::ApplyStyle(m_toolbar); }

int GitView::FindEntry(const SessionRef &ref) const {
  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].ref == ref) {
      return static_cast<int>(i);
    }
  }
  return wxNOT_FOUND;
}

GitView::Entry *GitView::ActiveEntry() {
  if (!m_active) {
    return nullptr;
  }
  const int index = FindEntry(*m_active);
  return index == wxNOT_FOUND ? nullptr : &m_entries[index];
}

void GitView::Request(Entry &entry) {
  if (!entry.hasFolder) {
    return;
  }
  // The driver (and its thread) starts when the session is first shown.
  if (!entry.driver) {
    entry.driver = MakeDriver(entry.account);
  }
  entry.driver->Status();
}

void GitView::OnSessionPageActivity(PageViewEvent &event) {
  event.Skip();
  RefreshView();
}

void GitView::OnSessionPageAdded(PageViewEvent &event) {
  event.Skip();
  if (FindEntry(event.GetRef()) != wxNOT_FOUND) {
    return;
  }
  Entry entry;
  entry.ref = event.GetRef();
  entry.account = event.GetAccount();
  entry.hasFolder = HasFolder(entry.account);
  entry.message = entry.hasFolder ? _("Loading...") : _("No working folder");
  m_entries.push_back(std::move(entry));
  if (m_active == m_entries.back().ref) {
    Request(m_entries.back());
    Refill();
  }
}

void GitView::OnSessionPageRemoved(PageViewEvent &event) {
  event.Skip();
  const int index = FindEntry(event.GetRef());
  if (index == wxNOT_FOUND) {
    return;
  }
  DeleteInBackground(std::move(m_entries[index].driver));
  m_entries.erase(m_entries.begin() + index);
  if (m_active == event.GetRef()) {
    m_active.reset();
    Refill();
  }
}

void GitView::OnSessionRenamed(PageViewEvent &event) {
  event.Skip();
  if (const int index = FindEntry(event.GetOldRef()); index != wxNOT_FOUND) {
    m_entries[index].ref = event.GetRef();
  }
  if (m_active == event.GetOldRef()) {
    m_active = event.GetRef();
  }
}

void GitView::OnSessionPagesCleared(wxCommandEvent &event) {
  event.Skip();
  for (Entry &entry : m_entries) {
    DeleteInBackground(std::move(entry.driver));
  }
  m_entries.clear();
  m_active.reset();
  Refill();
}

void GitView::OnSessionPageShown(PageViewEvent &event) {
  event.Skip();
  m_active = event.GetRef();
  if (Entry *entry = ActiveEntry()) {
    Request(*entry); // Shows what it has now, and the answer comes later
  }
  Refill();
}

void GitView::OnGitStatus(GitEvent &event) {
  event.Skip(); // The driver may be someone else's
  for (Entry &entry : m_entries) {
    if (!entry.driver || entry.driver->GetId() != event.GetInt()) {
      continue;
    }
    if (event.GetResult().ok()) {
      entry.status = event.GetRepoStatus();
      entry.message.clear();
    } else {
      KLOG_DEBUG() << "git status of " << entry.ref.name << ": "
                   << event.GetResult().message();
      entry.status = GitStatus{};
      entry.message = event.GetResult().message();
    }
    if (&entry == ActiveEntry()) {
      Refill();
    }
    return;
  }
}

void GitView::OnFileSaved(FileEvent &event) {
  event.Skip();
  // Only a file of the account of the session that shows can be in its
  // repository.
  Entry *entry = ActiveEntry();
  if (entry != nullptr &&
      event.GetAccount().GetId() == entry->account.GetId()) {
    Request(*entry);
  }
}

void GitView::RefreshView() {
  if (Entry *entry = ActiveEntry()) {
    Request(*entry);
  }
}

void GitView::OnRefresh(wxCommandEvent &event) {
  event.Skip();
  RefreshView();
}

void GitView::AutoSizeColumns() {
  for (unsigned int i = 0; i < m_list->GetColumnCount(); ++i) {
    m_list->GetColumn(i)->SetWidth(wxCOL_WIDTH_AUTOSIZE);
  }
}

void GitView::Refill() {
  wxWindowUpdateLocker locker{m_list};
  m_list->DeleteAllItems();
  m_rows.clear();
  const Entry *entry = ActiveEntry();
  if (entry == nullptr) {
    m_branch->SetLabel(wxEmptyString);
    return;
  }

  if (!entry->message.empty()) {
    m_branch->SetLabel(wxEmptyString);
    // The first line: the error of git can have many.
    m_list->AppendItem(
        {IconText(wxEmptyString), IconText(entry->message.BeforeFirst('\n'))});
    m_rows.emplace_back();
    Layout();
    AutoSizeColumns();
    return;
  }

  const GitStatus &status = entry->status;
  wxString label =
      status.detached ? wxString(_("(detached HEAD)")) : status.branch;
  if (status.ahead != 0 || status.behind != 0) {
    label += wxString::Format(_("  ahead %d, behind %d"), status.ahead,
                              status.behind);
  }
  m_branch->SetLabel(label);

  const auto &bitmaps = AppManager::Get().GetBitmaps();
  const bool dark = wxSystemSettings::GetAppearance().IsDark();
  auto add = [&](const wxString &text, const char *icon,
                 const GitChange &change, bool staged) {
    // The status, and the file with the icon of its type
    m_list->AppendItem(
        {IconText(text, bitmaps.GetByAlias(icon, false)),
         IconText(change.path, bitmaps.GetFileIcon(change.path, dark))});
    Row row;
    row.path = change.path;
    row.staged = staged;
    row.untracked = change.untracked;
    row.deleted = (staged ? change.index : change.worktree) == 'D';
    row.added = staged && change.index == 'A';
    m_rows.push_back(row);
  };
  // The files that are in the index, then those of the work tree.
  for (const GitChange &change : status.changes) {
    if (change.IsStaged()) {
      const StatusLook look = LookOf(change.index);
      add(look.word + " " + _("(staged)"), look.icon, change, true);
    }
  }
  for (const GitChange &change : status.changes) {
    if (change.unmerged) {
      add(_("Conflict"), "git-conflict", change, false);
    } else if (change.untracked) {
      add(_("Untracked"), "git-untracked", change, false);
    } else if (change.IsUnstaged()) {
      const StatusLook look = LookOf(change.worktree);
      add(look.word, look.icon, change, false);
    }
  }
  if (m_rows.empty()) {
    m_list->AppendItem({IconText(wxEmptyString), IconText(_("No changes"))});
    m_rows.emplace_back();
  }
  Layout();
  AutoSizeColumns();
}

const GitView::Row *GitView::RowOf(const wxDataViewItem &item) const {
  const int row = m_list->ItemToRow(item);
  if (row < 0 || row >= static_cast<int>(m_rows.size()) ||
      m_rows[row].path.empty()) {
    return nullptr; // A message, not a file
  }
  return &m_rows[row];
}

void GitView::OnActivated(wxDataViewEvent &event) {
  event.Skip();
  if (const Row *row = RowOf(event.GetItem())) {
    ShowDiff(*row);
  }
}

std::vector<const GitView::Row *> GitView::SelectedRows() const {
  wxDataViewItemArray items;
  m_list->GetSelections(items);
  std::vector<const Row *> rows;
  for (const wxDataViewItem &item : items) {
    if (const Row *row = RowOf(item)) {
      rows.push_back(row);
    }
  }
  return rows;
}

std::vector<wxString> GitView::PathSpecs(const std::vector<const Row *> &rows) {
  std::vector<wxString> specs;
  for (const Row *row : rows) {
    // Relative to the root of the repository, and a name, not a pattern.
    const wxString spec = ":(top,literal)" + row->path;
    // A file with a row in the index and one in the work tree is one file.
    if (std::find(specs.begin(), specs.end(), spec) == specs.end()) {
      specs.push_back(spec);
    }
  }
  return specs;
}

void GitView::OnContextMenu(wxDataViewEvent &event) {
  event.Skip();
  const Row *clicked = RowOf(event.GetItem());
  if (clicked == nullptr) {
    return;
  }
  if (!m_list->IsSelected(event.GetItem())) {
    m_list->UnselectAll();
    m_list->Select(event.GetItem());
  }
  const std::vector<const Row *> rows = SelectedRows();
  const bool single = rows.size() == 1;
  const bool anyUnstaged = std::any_of(rows.begin(), rows.end(),
                                       [](const Row *r) { return !r->staged; });

  wxMenu menu;
  if (single) {
    menu.Append(kIdShowDiff, _("Show Diff"));
    menu.Append(kIdOpenFile, _("Open File"))->Enable(!rows[0]->deleted);
    menu.AppendSeparator();
  }
  menu.Append(kIdAdd, _("Add"))->Enable(anyUnstaged);
  if (single) {
    menu.Append(kIdFileLog, _("Log"))->Enable(!rows[0]->untracked);
    menu.Append(kIdBlame, _("Blame"))
        ->Enable(!rows[0]->untracked && !rows[0]->added);
  }
  menu.AppendSeparator();
  menu.Append(kIdResetFile, _("Reset File..."));
  menu.Bind(wxEVT_MENU, &GitView::OnMenuAction, this);
  m_list->PopupMenu(&menu);
}

void GitView::OnMenuAction(wxCommandEvent &event) {
  event.Skip();
  const int id = event.GetId();
  const std::vector<const Row *> rows = SelectedRows();
  const Row *first = rows.empty() ? nullptr : rows[0];
  if (id == kIdShowDiff && first) {
    ShowDiff(*first);
  } else if (id == kIdOpenFile && first) {
    OpenFile(*first);
  } else if (id == kIdAdd) {
    AddFiles(rows);
  } else if (id == kIdFileLog && first) {
    ShowLog(first);
  } else if (id == kIdBlame && first) {
    ShowBlame(*first);
  } else if (id == kIdResetFile) {
    ResetFiles(rows);
  } else if (id == kIdCommit) {
    Commit();
  } else if (id == kIdPush) {
    Push();
  } else if (id == kIdRepoLog) {
    ShowLog(nullptr);
  } else if (id == kIdResetRepo) {
    ResetRepo();
  }
}

void GitView::AddFiles(const std::vector<const Row *> &rows) {
  Entry *entry = ActiveEntry();
  if (entry == nullptr || !entry->driver) {
    return;
  }
  std::vector<const Row *> unstaged;
  for (const Row *row : rows) {
    if (!row->staged) {
      unstaged.push_back(row);
    }
  }
  if (unstaged.empty()) {
    return;
  }
  std::vector<wxString> args = {"add", "--"};
  for (const wxString &spec : PathSpecs(unstaged)) {
    args.push_back(spec);
  }
  entry->driver->RunCommand("add", args);
}

void GitView::ResetFiles(const std::vector<const Row *> &rows) {
  Entry *entry = ActiveEntry();
  if (entry == nullptr || !entry->driver || rows.empty()) {
    return;
  }
  const std::vector<wxString> all = PathSpecs(rows);
  const wxString question =
      all.size() == 1
          ? wxString::Format(
                _("Discard all the changes of '%s'? This can not be undone."),
                rows[0]->path)
          : wxString::Format(
                _("Discard all the changes of these %d files? This can not be "
                  "undone."),
                static_cast<int>(all.size()));
  if (::wxMessageBox(question, "Kennel",
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
    return;
  }
  // A new file is deleted, an untracked one is cleaned, all the others get the
  // content of HEAD (in the index and in the work tree).
  std::vector<const Row *> tracked, added, untracked;
  for (const Row *row : rows) {
    (row->untracked ? untracked : row->added ? added : tracked).push_back(row);
  }
  const auto run = [&](std::vector<wxString> args,
                       const std::vector<const Row *> &group) {
    if (group.empty()) {
      return;
    }
    args.push_back("--");
    for (const wxString &spec : PathSpecs(group)) {
      args.push_back(spec);
    }
    entry->driver->RunCommand("reset-file", args);
  };
  run({"restore", "--source=HEAD", "--staged", "--worktree"}, tracked);
  run({"rm", "-f", "-q"}, added);
  run({"clean", "-f", "-q"}, untracked);
}

void GitView::ResetRepo() {
  Entry *entry = ActiveEntry();
  if (entry == nullptr || !entry->driver) {
    return;
  }
  if (::wxMessageBox(
          _("Discard all the changes of the repository (staged and not "
            "staged) and go back to HEAD? Untracked files are kept. This can "
            "not be undone."),
          "Kennel", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
    return;
  }
  entry->driver->RunCommand("reset-repo", {"reset", "--hard", "HEAD"});
}

void GitView::ShowLog(const Row *row) {
  Entry *entry = ActiveEntry();
  if (entry == nullptr || !entry->driver) {
    return;
  }
  const long lines =
      ::wxGetNumberFromUser(_("Number of log entries to show:"), _("Entries:"),
                            _("Git Log"), m_logLines, 1, 100000, this);
  if (lines < 0) {
    return; // Cancelled
  }
  m_logLines = lines;
  std::vector<wxString> args = {"log", "--no-color", "-n",
                                wxString::Format("%ld", lines)};
  if (row != nullptr) {
    args.push_back("--follow");
    args.push_back("--");
    args.push_back(":(top,literal)" + row->path);
  }
  entry->driver->RunCommand("log", args, row ? row->path : wxString());
}

void GitView::ShowBlame(const Row &row) {
  Entry *entry = ActiveEntry();
  if (entry == nullptr || !entry->driver || entry->status.root.empty()) {
    return;
  }
  // blame has no pathspecs: it runs in the root, where the path is right.
  entry->driver->RunCommand(
      "blame",
      {"-C", entry->status.root, "blame", "--date=short", "--", row.path},
      row.path);
}

void GitView::Commit() {
  Entry *entry = ActiveEntry();
  if (entry == nullptr || !entry->driver || entry->status.root.empty()) {
    return;
  }
  // The parent is the main frame, so the dialog is in the middle of it.
  GitCommitMessageDlg dlg(wxGetTopLevelParent(this));
  dlg.CentreOnParent();
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  wxString message = dlg.GetStcCommitMessage()->GetText();
  message.Trim().Trim(false);
  const bool amend = dlg.GetCheckBoxAmend()->GetValue();
  const bool signoff = dlg.GetCheckBoxSignoff()->GetValue();
  if (message.empty() && !amend) {
    ::wxMessageBox(_("The commit message is empty"), "Kennel",
                   wxOK | wxICON_ERROR, this);
    return;
  }
  std::vector<wxString> args = {"commit"};
  if (amend) {
    args.push_back("--amend");
  }
  if (signoff) {
    args.push_back("--signoff");
  }
  if (message.empty()) {
    args.push_back("--no-edit");
  } else {
    args.push_back("-m");
    args.push_back(message);
  }
  // Runs in the background; a failure comes back as an error (OnGitCommand()).
  entry->driver->RunCommand("commit", args);
}

void GitView::Push() {
  const Entry *entry = ActiveEntry();
  if (entry == nullptr || entry->status.root.empty()) {
    return;
  }
  // The terminal of the account runs it (the git of a WSL distribution or of a
  // remote host asks for its passwords there). Only the terminal knows when it
  // is done: nothing is refreshed here.
  const wxString &root = entry->status.root;
  FileEvent run{wxEVT_GITVIEW_RUN_IN_TERMINAL};
  run.SetEventObject(this);
  run.SetPath(root);
  run.SetAccount(entry->account);
  run.SetString(entry->account.IsLocalHost()
                    ? "git -C " + QuoteForShell(root) + " push"
                    : "cd " + QuoteForShell(root) + " && git push");
  EventNotifier::Get()->AddPendingEvent(run);
}

void GitView::OnGitCommand(GitEvent &event) {
  event.Skip(); // The driver may be someone else's
  Entry *entry = nullptr;
  for (Entry &candidate : m_entries) {
    if (candidate.driver && candidate.driver->GetId() == event.GetInt()) {
      entry = &candidate;
      break;
    }
  }
  if (entry == nullptr) {
    return;
  }
  const wxString &command = event.GetCommand();
  if (!event.GetResult().ok()) {
    ::wxMessageBox(event.GetResult().message(), "Kennel", wxOK | wxICON_ERROR,
                   this);
  } else if (command == "log" || command == "blame") {
    FileEvent show{wxEVT_GITVIEW_SHOW_TEXT};
    show.SetEventObject(this);
    show.SetAccount(entry->account);
    // The log of the repository has no file: a name in its folder.
    show.SetPath(
        FullPath(entry->account, entry->status.root,
                 event.GetPath().empty() ? wxString("git") : event.GetPath()));
    show.SetInt(command == "log" ? 0 : 1);
    show.SetString(event.GetText().empty() ? wxString(_("(nothing to show)"))
                                           : event.GetText());
    EventNotifier::Get()->AddPendingEvent(show);
    return;
  }
  // The files changed, also after an error (a part of the work may be done).
  Request(*entry);
}

void GitView::OpenFile(const Row &row) {
  const Entry *entry = ActiveEntry();
  if (entry == nullptr || entry->status.root.empty()) {
    return;
  }
  FileEvent open{wxEVT_FILETREE_ACTIVATED};
  open.SetEventObject(this);
  open.SetPath(FullPath(entry->account, entry->status.root, row.path));
  open.SetIsDir(false);
  open.SetAccount(entry->account);
  EventNotifier::Get()->AddPendingEvent(open);
}

void GitView::ShowDiff(const Row &row) {
  Entry *entry = ActiveEntry();
  if (entry == nullptr || !entry->driver) {
    return;
  }
  // The answer comes on wxEVT_GIT_DIFF (see OnGitDiff()).
  entry->driver->Diff(row.path, row.staged, row.untracked);
}

void GitView::OnGitDiff(GitEvent &event) {
  event.Skip(); // The driver may be someone else's
  for (const Entry &entry : m_entries) {
    if (!entry.driver || entry.driver->GetId() != event.GetInt()) {
      continue;
    }
    if (!event.GetResult().ok()) {
      ::wxMessageBox(event.GetResult().message(), "Kennel", wxOK | wxICON_ERROR,
                     this);
      return;
    }
    // The host shows it (see MainNotebook).
    FileEvent show{wxEVT_GITVIEW_SHOW_DIFF};
    show.SetEventObject(this);
    show.SetPath(FullPath(entry.account, entry.status.root, event.GetPath()));
    show.SetAccount(entry.account);
    show.SetInt(event.IsStaged() ? 1 : 0);
    show.SetString(event.GetText().empty() ? wxString(_("(no differences)"))
                                           : event.GetText());
    EventNotifier::Get()->AddPendingEvent(show);
    return;
  }
}
