#include "app/FileExplorerView.hpp"

#include "app/ToolBarUtils.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Logger.h"
#include "core/WslPath.h"

#include "app/PageNames.h"
#include <algorithm>
#include <wx/aui/auibar.h>
#include <wx/choice.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(wxEVT_FILEEXPLORER_SESSION_SELECTED, PageViewEvent);

namespace {
const int kIdLink = wxWindow::NewControlId();
// What the choice of the file explorer shows for a session: the name, and the
// group when the session is in one.
wxString ExplorerLabel(const SessionRef &ref) {
  return ref.groupName == kTerminalsGroupName
             ? ref.name
             : ref.name + " (" + ref.groupName + ")";
}
} // namespace

FileExplorerView::FileExplorerView(wxWindow *parent) : wxPanel(parent) {
  m_toolbar = new wxAuiToolBar(this);
  const auto &bitmaps = AppManager::Get().GetBitmaps();
  toolbar::AddTool(m_toolbar, kIdLink, _("Link with Session"),
                   bitmaps.GetByAlias("link", false),
                   _("Link with Session: show the files of the session you are "
                     "working in"),
                   wxITEM_CHECK);
  m_toolbar->ToggleTool(kIdLink, true);
  m_toolbar->Realize();
  toolbar::ApplyStyle(m_toolbar);

  m_choice = new wxChoice(this, wxID_ANY);
  m_choice->Disable(); // No session yet
  m_book = new wxSimplebook(this);

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_toolbar, wxSizerFlags(0).Expand());
  sizer->Add(m_choice, wxSizerFlags(0).Expand().Border(wxALL, FromDIP(5)));
  sizer->Add(m_book, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  m_choice->Bind(wxEVT_CHOICE, &FileExplorerView::OnChoice, this);
  // The remote trees answer on the notifier (see ShowEntry()).
  m_toolbar->Bind(wxEVT_TOOL, &FileExplorerView::OnLink, this, kIdLink);
  EventNotifier::Get()->Bind(wxEVT_FILETREE_CONNECTED,
                             &FileExplorerView::OnTreeConnected, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGE_RENAMED,
                             &FileExplorerView::OnSessionRenamed, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGE_ADDED,
                             &FileExplorerView::OnSessionPageAdded, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGE_REMOVED,
                             &FileExplorerView::OnSessionPageRemoved, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGES_CLEARED,
                             &FileExplorerView::OnSessionPagesCleared, this);
  EventNotifier::Get()->Bind(wxEVT_SESSION_PAGE_SHOWN,
                             &FileExplorerView::OnSessionPageShown, this);
  EventNotifier::Get()->Bind(wxEVT_FILE_REVEAL, &FileExplorerView::OnFileReveal,
                             this);
}

FileExplorerView::~FileExplorerView() {
  EventNotifier::Get()->Unbind(wxEVT_FILETREE_CONNECTED,
                               &FileExplorerView::OnTreeConnected, this);
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGE_RENAMED,
                               &FileExplorerView::OnSessionRenamed, this);
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGE_ADDED,
                               &FileExplorerView::OnSessionPageAdded, this);
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGE_REMOVED,
                               &FileExplorerView::OnSessionPageRemoved, this);
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGES_CLEARED,
                               &FileExplorerView::OnSessionPagesCleared, this);
  EventNotifier::Get()->Unbind(wxEVT_SESSION_PAGE_SHOWN,
                               &FileExplorerView::OnSessionPageShown, this);
  EventNotifier::Get()->Unbind(wxEVT_FILE_REVEAL,
                               &FileExplorerView::OnFileReveal, this);
}

bool FileExplorerView::IsLinked() const {
  return m_toolbar->GetToolToggled(kIdLink);
}

void FileExplorerView::ApplyToolBarStyle() { toolbar::ApplyStyle(m_toolbar); }

int FileExplorerView::FindEntry(const SessionRef &ref) const {
  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].ref == ref) {
      return static_cast<int>(i);
    }
  }
  return wxNOT_FOUND;
}

void FileExplorerView::AddSession(const SessionRef &ref, const wxString &label,
                                  const Account &account) {
  if (FindEntry(ref) != wxNOT_FOUND) {
    return;
  }
  Entry entry;
  entry.ref = ref;
  entry.label = label;
  entry.account = account;
  // A remote tree is connected when it is shown for the first time: see
  // ShowEntry().
  entry.tree = new FileTreeCtrl(m_book, account);
  m_book->AddPage(entry.tree, label, false);
  m_choice->Append(label);
  m_entries.push_back(entry);
  m_choice->Enable();

  const int index = static_cast<int>(m_entries.size()) - 1;
  // The first session is shown, and so is the one the user is working in.
  if (m_entries.size() == 1 || (IsLinked() && m_active == ref)) {
    ShowEntry(index);
  }
}

void FileExplorerView::RemoveSession(const SessionRef &ref) {
  const int index = FindEntry(ref);
  if (index == wxNOT_FOUND) {
    return;
  }
  const bool wasShown = m_choice->GetSelection() == index;
  // The answer of a connection that is still made is dropped (no entry has its
  // request any more).
  const int page = m_book->FindPage(m_entries[index].tree);
  m_entries.erase(m_entries.begin() + index);
  m_choice->Delete(index);
  if (page != wxNOT_FOUND) {
    m_book->DeletePage(page);
  }
  if (m_entries.empty()) {
    m_choice->Disable();
  } else if (wasShown) {
    ShowEntry(std::min(index, static_cast<int>(m_entries.size()) - 1));
  }
  if (m_active == ref) {
    m_active.reset();
  }
}

void FileExplorerView::RenameSession(const SessionRef &from,
                                     const SessionRef &to,
                                     const wxString &label) {
  const int index = FindEntry(from);
  if (index == wxNOT_FOUND) {
    return;
  }
  Entry &entry = m_entries[index];
  entry.ref = to;
  entry.label = label;
  m_choice->SetString(index, label);
  if (const int page = m_book->FindPage(entry.tree); page != wxNOT_FOUND) {
    m_book->SetPageText(page, label);
  }
  if (m_active == from) {
    m_active = to;
  }
}

void FileExplorerView::Clear() {
  m_entries.clear();
  m_book->DeleteAllPages();
  m_choice->Clear();
  m_choice->Disable();
  m_active.reset();
}

void FileExplorerView::SetActiveSession(const SessionRef &ref) {
  m_active = ref;
  if (!IsLinked()) {
    return;
  }
  if (const int index = FindEntry(ref); index != wxNOT_FOUND) {
    ShowEntry(index);
  }
}

void FileExplorerView::ShowEntry(int index) {
  if (index < 0 || index >= static_cast<int>(m_entries.size())) {
    return;
  }
  Entry &entry = m_entries[index];
  m_choice->SetSelection(index);
  if (const int page = m_book->FindPage(entry.tree); page != wxNOT_FOUND) {
    m_book->SetSelection(page);
  }
  if (entry.account.IsRemote() && !entry.connectStarted) {
    entry.connectStarted = true;
    entry.connectRequestId = entry.tree->ConnectAsync();
  }
}

void FileExplorerView::OnTreeConnected(FileTreeConnectedEvent &event) {
  event.Skip(); // Other listeners of the notifier may own the request
  const int requestId = event.GetRequestId();
  const auto it = std::find_if(
      m_entries.begin(), m_entries.end(),
      [requestId](const Entry &e) { return e.connectRequestId == requestId; });
  if (it == m_entries.end()) {
    return; // Not ours, or the session is gone (and its tree with it)
  }
  Entry &entry = *it;
  entry.connectRequestId = 0;
  if (!entry.tree->FinishConnect(event)) {
    KLOG_WARN() << "Could not connect to " << entry.account.GetDisplayName()
                << ": " << event.GetStatus().message();
    entry.connectStarted = false; // Tried again when it is shown again
  }
}

void FileExplorerView::RevealFile(const Account &account,
                                  const wxString &filePath) {
  if (!IsLinked() || !IsShownOnScreen() || account.IsRemote()) {
    return;
  }
  const int index = m_choice->GetSelection();
  if (index == wxNOT_FOUND) {
    return;
  }
  const Entry &entry = m_entries[index];
  // The path as the tree writes it. A tree of a remote host is never listed.
  wxString path;
  if (entry.account.GetId() == account.GetId()) {
    path = filePath;
  } else if (entry.account.IsLocalHost() && account.IsWSL()) {
    path = wsl::ToWindowsPath(account.m_name, filePath);
  } else if (entry.account.IsWSL() && account.IsLocalHost()) {
    if (!wsl::ToLinuxPath(entry.account.m_name, filePath, &path)) {
      return;
    }
  } else {
    return;
  }
  entry.tree->SelectPath(path);
}

void FileExplorerView::OnChoice(wxCommandEvent &event) {
  event.Skip();
  const int index = m_choice->GetSelection();
  if (index == wxNOT_FOUND) {
    return;
  }
  ShowEntry(index);
  // The user picked the session: the host shows it.
  PageViewEvent picked(wxEVT_FILEEXPLORER_SESSION_SELECTED);
  picked.SetEventObject(this);
  picked.SetRef(m_entries[index].ref);
  EventNotifier::Get()->AddPendingEvent(picked);
}

void FileExplorerView::OnLink(wxCommandEvent &event) {
  event.Skip();
  // Linked again: go back to the session the user is working in.
  if (IsLinked() && m_active) {
    SetActiveSession(*m_active);
  }
}

void FileExplorerView::OnSessionPageRemoved(PageViewEvent &event) {
  event.Skip();
  RemoveSession(event.GetRef());
}

void FileExplorerView::OnSessionPageAdded(PageViewEvent &event) {
  event.Skip();
  const SessionRef &ref = event.GetRef();
  AddSession(ref, ExplorerLabel(ref), event.GetAccount());
}

void FileExplorerView::OnSessionPageShown(PageViewEvent &event) {
  event.Skip();
  SetActiveSession(event.GetRef());
}

void FileExplorerView::OnSessionRenamed(PageViewEvent &event) {
  event.Skip();
  RenameSession(event.GetOldRef(), event.GetRef(),
                ExplorerLabel(event.GetRef()));
}

void FileExplorerView::OnFileReveal(FileEvent &event) {
  event.Skip();
  RevealFile(event.GetAccount(), event.GetPath());
}
