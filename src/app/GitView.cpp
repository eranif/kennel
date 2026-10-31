#include "app/GitView.hpp"

#include "app/FileTreeCtrl.hpp"
#include "app/ToolBarUtils.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/GitDriverLocal.h"
#include "core/GitDriverSSH.h"
#include "core/GitDriverWSL.h"
#include "core/Logger.h"

#include <wx/app.h>
#include <wx/aui/auibar.h>
#include <wx/dataview.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <thread>

namespace {
const int kIdRefresh = wxWindow::NewControlId();

// The letter of `git status` as a word.
wxString StatusWord(char letter) {
  switch (letter) {
  case 'M':
    return _("Modified");
  case 'T':
    return _("Type changed");
  case 'A':
    return _("New");
  case 'D':
    return _("Deleted");
  case 'R':
    return _("Renamed");
  case 'C':
    return _("Copied");
  default:
    return _("Changed");
  }
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
} // namespace

GitView::GitView(wxWindow *parent) : wxPanel(parent) {
  m_toolbar = new wxAuiToolBar(this);
  toolbar::AddTool(m_toolbar, kIdRefresh, _("Refresh"),
                   AppManager::Get().GetBitmaps().GetByAlias("reload", false),
                   _("Refresh the git status"));
  m_toolbar->Realize();
  toolbar::ApplyStyle(m_toolbar);

  m_branch = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_list = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxDV_SINGLE | wxDV_ROW_LINES);
  StylePageView(m_list);
  m_list->AppendTextColumn(_("Status"), wxDATAVIEW_CELL_INERT, 130);
  m_list->AppendTextColumn(_("Path"), wxDATAVIEW_CELL_INERT, 300);

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_toolbar, wxSizerFlags(0).Expand());
  sizer->Add(m_branch, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(5)));
  sizer->Add(m_list, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  m_toolbar->Bind(wxEVT_TOOL, &GitView::OnRefresh, this, kIdRefresh);
  m_list->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &GitView::OnActivated, this);
  auto *notifier = EventNotifier::Get();
  notifier->Bind(wxEVT_SESSION_PAGE_ADDED, &GitView::OnSessionPageAdded, this);
  notifier->Bind(wxEVT_SESSION_PAGE_REMOVED, &GitView::OnSessionPageRemoved,
                 this);
  notifier->Bind(wxEVT_SESSION_PAGE_RENAMED, &GitView::OnSessionRenamed, this);
  notifier->Bind(wxEVT_SESSION_PAGE_SHOWN, &GitView::OnSessionPageShown, this);
  notifier->Bind(wxEVT_SESSION_PAGES_CLEARED, &GitView::OnSessionPagesCleared,
                 this);
  notifier->Bind(wxEVT_GIT_STATUS, &GitView::OnGitStatus, this);
  // Sent to the application (see FilePage), not to the notifier.
  wxTheApp->Bind(wxEVT_FILE_SAVE_DONE, &GitView::OnFileSaveDone, this);
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
  if (wxTheApp != nullptr) {
    wxTheApp->Unbind(wxEVT_FILE_SAVE_DONE, &GitView::OnFileSaveDone, this);
  }
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

void GitView::OnFileSaveDone(FileEvent &event) {
  event.Skip();
  // Only a file of the account of the session that shows can be in its
  // repository.
  Entry *entry = ActiveEntry();
  if (entry != nullptr &&
      event.GetAccount().GetId() == entry->account.GetId()) {
    Request(*entry);
  }
}

void GitView::OnRefresh(wxCommandEvent &event) {
  event.Skip();
  if (Entry *entry = ActiveEntry()) {
    Request(*entry);
  }
}

void GitView::Refill() {
  m_list->DeleteAllItems();
  m_rowPaths.clear();
  const Entry *entry = ActiveEntry();
  if (entry == nullptr) {
    m_branch->SetLabel(wxEmptyString);
    return;
  }

  if (!entry->message.empty()) {
    m_branch->SetLabel(wxEmptyString);
    // The first line: the error of git can have many.
    m_list->AppendItem({wxString(), entry->message.BeforeFirst('\n')});
    m_rowPaths.emplace_back();
    Layout();
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

  auto add = [this](const wxString &state, const GitChange &change) {
    m_list->AppendItem({state, change.path});
    m_rowPaths.push_back(change.path);
  };
  // The files that are in the index, then those of the work tree.
  for (const GitChange &change : status.changes) {
    if (change.IsStaged()) {
      add(StatusWord(change.index) + " " + _("(staged)"), change);
    }
  }
  for (const GitChange &change : status.changes) {
    if (change.unmerged) {
      add(_("Conflict"), change);
    } else if (change.untracked) {
      add(_("Untracked"), change);
    } else if (change.IsUnstaged()) {
      add(StatusWord(change.worktree), change);
    }
  }
  if (m_rowPaths.empty()) {
    m_list->AppendItem({wxString(), _("No changes")});
    m_rowPaths.emplace_back();
  }
  Layout();
}

void GitView::OnActivated(wxDataViewEvent &event) {
  event.Skip();
  const int row = m_list->ItemToRow(event.GetItem());
  const Entry *entry = ActiveEntry();
  if (entry == nullptr || row < 0 ||
      row >= static_cast<int>(m_rowPaths.size()) || m_rowPaths[row].empty() ||
      entry->status.root.empty()) {
    return;
  }
  wxString path = entry->status.root + "/" + m_rowPaths[row];
  if (entry->account.IsLocalHost()) {
    path = wxFileName(path).GetFullPath(); // The separators of this machine
  }
  FileEvent open{wxEVT_FILETREE_ACTIVATED};
  open.SetEventObject(this);
  open.SetPath(path);
  open.SetIsDir(false);
  open.SetAccount(entry->account);
  EventNotifier::Get()->AddPendingEvent(open);
}
