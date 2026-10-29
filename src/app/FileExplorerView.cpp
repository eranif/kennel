#include "app/FileExplorerView.hpp"

#include "app/ToolBarUtils.h"
#include "core/AppManager.h"

#include <wx/aui/auibar.h>
#include <wx/choice.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

wxDEFINE_EVENT(wxEVT_FILEEXPLORER_SESSION_SELECTED, PageViewEvent);

namespace {
const int kIdLink = wxWindow::NewControlId();
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
  sizer->Add(m_choice, wxSizerFlags(0).Expand());
  sizer->Add(m_book, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  m_choice->Bind(wxEVT_CHOICE, &FileExplorerView::OnChoice, this);
  m_toolbar->Bind(wxEVT_TOOL, &FileExplorerView::OnLink, this, kIdLink);
}

FileExplorerView::~FileExplorerView() = default;

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

int FileExplorerView::FindEntryById(int id) const {
  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return wxNOT_FOUND;
}

wxWindow *FileExplorerView::NewMessagePage(const wxString &text,
                                           wxStaticText **label) {
  auto *page = new wxPanel(m_book);
  *label = new wxStaticText(page, wxID_ANY, text, wxDefaultPosition,
                            wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->AddStretchSpacer(1);
  sizer->Add(*label, wxSizerFlags(0).Expand().Border(wxALL, 10));
  sizer->AddStretchSpacer(1);
  page->SetSizer(sizer);
  return page;
}

void FileExplorerView::AddSession(const SessionRef &ref, const wxString &label,
                                  const Account &account) {
  if (FindEntry(ref) != wxNOT_FOUND) {
    return;
  }
  Entry entry;
  entry.id = m_nextId++;
  entry.ref = ref;
  entry.label = label;
  entry.account = account;
  if (account.IsRemote()) {
    // Connected when it is shown for the first time: see ShowEntry()
    entry.page = NewMessagePage(
        wxString::Format(_("The files of %s are shown when you select this "
                           "session"),
                         account.GetDisplayName()),
        &entry.message);
  } else {
    entry.tree = new FileTreeCtrl(m_book, account);
    entry.page = entry.tree;
  }
  m_book->AddPage(entry.page, label, false);
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
  const int page = m_book->FindPage(m_entries[index].page);
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
  if (const int page = m_book->FindPage(entry.page); page != wxNOT_FOUND) {
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
  if (const int page = m_book->FindPage(entry.page); page != wxNOT_FOUND) {
    m_book->SetSelection(page);
  }
  if (entry.account.IsRemote() && entry.tree == nullptr &&
      !entry.connectStarted) {
    entry.connectStarted = true;
    entry.message->SetLabel(wxString::Format(_("Connecting to %s..."),
                                             entry.account.GetDisplayName()));
    entry.message->GetParent()->Layout();
    // After the message is on the screen: connecting waits for the host.
    CallAfter(&FileExplorerView::ConnectEntry, entry.id);
  }
}

void FileExplorerView::ConnectEntry(int id) {
  int index = FindEntryById(id);
  if (index == wxNOT_FOUND) {
    return;
  }
  const Account account = m_entries[index].account;
  const wxString label = m_entries[index].label;
  wxWindow *messagePage = m_entries[index].page;
  messagePage->Update(); // Paint the message before waiting for the host

  auto *tree = new FileTreeCtrl(m_book, account);
  tree->Hide();
  const bool connected = tree->Connect();

  // Connecting can run the event loop (a message box): the session may be gone.
  index = FindEntryById(id);
  if (index == wxNOT_FOUND) {
    tree->Destroy();
    return;
  }
  Entry &entry = m_entries[index];
  if (!connected) {
    tree->Destroy();
    entry.message->SetLabel(wxString::Format(_("Could not connect to %s"),
                                             account.GetDisplayName()));
    entry.message->GetParent()->Layout();
    return; // It keeps the message: the next try is the user's
  }
  // The tree takes the place of the message.
  const int page = m_book->FindPage(entry.page);
  const bool shown = m_book->GetSelection() == page;
  m_book->InsertPage(page, tree, label, shown);
  m_book->DeletePage(page + 1);
  entry.page = tree;
  entry.tree = tree;
  entry.message = nullptr;
  if (shown) {
    m_book->SetSelection(page);
  }
}

void FileExplorerView::RevealFile(const Account &account,
                                  const wxString &path) {
  if (!IsLinked() || account.IsRemote() || !IsShownOnScreen()) {
    return;
  }
  const int index = m_choice->GetSelection();
  if (index == wxNOT_FOUND) {
    return;
  }
  const Entry &entry = m_entries[index];
  if (entry.tree != nullptr && entry.account.GetId() == account.GetId()) {
    entry.tree->SelectPath(path);
  }
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
  ProcessWindowEvent(picked);
}

void FileExplorerView::OnLink(wxCommandEvent &event) {
  event.Skip();
  // Linked again: go back to the session the user is working in.
  if (IsLinked() && m_active) {
    SetActiveSession(*m_active);
  }
}
