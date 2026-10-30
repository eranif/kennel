#include "app/MainNotebook.hpp"

#include "GlyphTabArt.h"
#include "StartAgentDialog.hpp"
#include "ThemeLoader.h"
#include "ThemeManager.h"
#include "app/ActivityView.hpp"
#include "app/AppTheme.h"
#include "app/AssetBootstrap.h"
#include "app/FileExplorerView.hpp"
#include "app/FilePage.hpp"
#include "app/GotoAnythingBar.hpp"
#include "app/MainFrame.h"
#include "app/PageNames.h"
#include "app/PageSwitcherDlg.hpp"
#include "app/SessionPage.hpp"
#include "app/SettingsDlg.hpp"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/EventNotifier.hpp"
#include "core/JobLog.h"
#include "core/Logger.h"
#include "core/SftpClient.h"
#include "core/Workspace.h"
#include "core/WorkspaceStore.h"
#include "core/WslPath.h"
#include "terminal_view.h"

#include "core/Helpers.h"
#include <algorithm>
#include <thread>
#include <wx/dir.h>
#include <wx/fontdlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>
#include <wx/utils.h>
#include <wx/xrc/xmlres.h>

namespace {
wxString TitleOf(wxWindow *page) {
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    return session->GetSession().name;
  }
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    return file->GetDisplayName();
  }
  return wxString{};
}

// The icons of the tabs are made for menus (16 DIP): this bundle shows them
// bigger. The source makes the bitmap at the size that is asked for.
constexpr int kTabIconDip = 24;

class TabIconImpl : public wxBitmapBundleImpl {
public:
  explicit TabIconImpl(const wxBitmapBundle &source) : m_source(source) {}

  wxSize GetDefaultSize() const override {
    return wxSize(kTabIconDip, kTabIconDip);
  }

  wxSize GetPreferredBitmapSizeAtScale(double scale) const override {
    const int size = wxRound(kTabIconDip * scale);
    return wxSize(size, size);
  }

  wxBitmap GetBitmap(const wxSize &size) override {
    return m_source.GetBitmap(size);
  }

private:
  wxBitmapBundle m_source;
};

wxBitmapBundle TabIcon(const wxBitmapBundle &source) {
  return source.IsOk() ? wxBitmapBundle::FromImpl(new TabIconImpl(source))
                       : wxBitmapBundle{};
}

wxBitmapBundle IconOf(wxWindow *page) {
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    return TabIcon(SessionIconFor(session->GetSession()));
  }
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    return TabIcon(AppManager::Get().GetBitmaps().GetFileIcon(
        file->GetPath(), wxSystemSettings::GetAppearance().IsDark()));
  }
  return wxBitmapBundle{};
}
} // namespace

namespace {
// The key of a file page (see FileEvent::MakeKey) for a saved file.
wxString KeyOfFile(const SessionFile &file) {
  return FileEvent::MakeKey(file.path, {file.remoteHost, file.remoteUser});
}

void PushRecent(std::vector<wxString> &list, const wxString &value,
                size_t maxSize = 10) {
  if (value.empty()) {
    return;
  }
  list.erase(std::remove(list.begin(), list.end(), value), list.end());
  list.insert(list.begin(), value);
  if (list.size() > maxSize) {
    list.resize(maxSize);
  }
}
} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MainNotebook::MainNotebook(wxWindow *parent, wxWindowID winid,
                           const wxPoint &pos, const wxSize &size, long style,
                           const wxString &name)
    : wxPanel(parent, winid, pos, size, style, name),
      m_registry(&AppManager::Get().Adapters()),
      m_workspaceStore(&AppManager::Get().Workspace()),
      m_paths(AppManager::Get().Paths()) {
  SetSizer(new wxBoxSizer(wxVERTICAL));
  // Above the pages; hidden until it is asked for.
  m_gotoBar = new GotoAnythingBar(this);
  GetSizer()->Add(m_gotoBar, wxSizerFlags(0).Expand());
  m_gotoBar->Hide();

  m_book = new GlyphBook(
      this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxAUI_NB_TAB_SPLIT | wxAUI_NB_CLOSE_ON_ACTIVE_TAB | wxAUI_NB_TAB_MOVE |
          wxAUI_NB_WINDOWLIST_BUTTON | wxAUI_NB_MIDDLE_CLICK_CLOSE |
          wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_LEFT |
          wxBORDER_DEFAULT);

  GetSizer()->Add(m_book, wxSizerFlags(1).Expand());
  Layout();

  if (const wxString path = ResolveIconPath("hand.svg");
      !path.empty() && wxFileName::FileExists(path)) {
    m_handIcon = wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
  }
  if (const wxString path = ResolveIconPath("check.svg");
      !path.empty() && wxFileName::FileExists(path)) {
    m_checkIcon = wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
  }

  m_book->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &MainNotebook::OnPageChanged,
               this);
  m_book->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE, &MainNotebook::OnPageClose, this);
  // Runs before the book's own handler, which selects the page the focus went
  // into; not skipped, so a focus never changes the selected page.
  m_book->Bind(wxEVT_CHILD_FOCUS, &MainNotebook::OnChildFocus, this);
  // What the file explorer asks for, and the theme, come on the EventNotifier.
  EventNotifier::Get()->Bind(wxEVT_THEME_APPLIED, &MainNotebook::OnThemeApplied,
                             this);
  EventNotifier::Get()->Bind(wxEVT_FILEEXPLORER_SESSION_SELECTED,
                             &MainNotebook::OnShowSessionRequested, this);
  EventNotifier::Get()->Bind(wxEVT_ACTIVITYVIEW_SESSION_SELECTED,
                             &MainNotebook::OnShowSessionRequested, this);
  EventNotifier::Get()->Bind(wxEVT_FILETREE_ACTIVATED,
                             &MainNotebook::OnFileTreeActivated, this);
  Bind(wxEVT_GOTOANYTHING_CHANGED, &MainNotebook::OnGotoChanged, this);
  Bind(wxEVT_GOTOANYTHING_CLOSED, &MainNotebook::OnGotoClosed, this);
  Bind(wxEVT_SESSION_IDLE, &MainNotebook::OnSessionIdle, this);
  Bind(wxEVT_PAGE_SWITCHER_DONE, &MainNotebook::OnPageSwitcherDone, this);
  Bind(wxEVT_SESSION_ACTIVE, &MainNotebook::OnSessionActive, this);
  Bind(wxEVT_SESSION_EXITED, &MainNotebook::OnSessionExited, this);
  Bind(wxEVT_SESSION_PROGRAM, &MainNotebook::OnSessionProgram, this);
  Bind(wxEVT_SESSION_CWD, &MainNotebook::OnSessionCwd, this);
  Bind(wxEVT_IDLE, &MainNotebook::OnIdleEvent, this);
  // A FilePage handles its own save events first and then Skip()s them, so
  // they propagate up to wxTheApp, where the notebook does its part (activity
  // indicator, closing after a save).
  wxTheApp->Bind(wxEVT_FILE_SAVE_STARTED, &MainNotebook::OnFileSaveStarted,
                 this);
  wxTheApp->Bind(wxEVT_FILE_SAVE_DONE, &MainNotebook::OnFileSaveDone, this);
}

MainNotebook::~MainNotebook() {
  EventNotifier::Get()->Unbind(wxEVT_THEME_APPLIED,
                               &MainNotebook::OnThemeApplied, this);
  EventNotifier::Get()->Unbind(wxEVT_FILEEXPLORER_SESSION_SELECTED,
                               &MainNotebook::OnShowSessionRequested, this);
  EventNotifier::Get()->Unbind(wxEVT_ACTIVITYVIEW_SESSION_SELECTED,
                               &MainNotebook::OnShowSessionRequested, this);
  EventNotifier::Get()->Unbind(wxEVT_FILETREE_ACTIVATED,
                               &MainNotebook::OnFileTreeActivated, this);
  Unbind(wxEVT_SESSION_IDLE, &MainNotebook::OnSessionIdle, this);
  Unbind(wxEVT_SESSION_ACTIVE, &MainNotebook::OnSessionActive, this);
  Unbind(wxEVT_SESSION_EXITED, &MainNotebook::OnSessionExited, this);
  Unbind(wxEVT_SESSION_PROGRAM, &MainNotebook::OnSessionProgram, this);
  Unbind(wxEVT_SESSION_CWD, &MainNotebook::OnSessionCwd, this);
  Unbind(wxEVT_IDLE, &MainNotebook::OnIdleEvent, this);
  if (wxTheApp) {
    wxTheApp->Unbind(wxEVT_FILE_SAVE_STARTED, &MainNotebook::OnFileSaveStarted,
                     this);
    wxTheApp->Unbind(wxEVT_FILE_SAVE_DONE, &MainNotebook::OnFileSaveDone, this);
  }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void MainNotebook::OnPageChanged(wxAuiNotebookEvent &event) {
  const int index = event.GetSelection();
  if (index == wxNOT_FOUND) {
    return;
  }
  m_book->GetCurrentPage()->CallAfter(&wxWindow::SetFocus);

  // A click on a tab, or the focus that went into a page: the book shows it
  // already, so it is only told.
  PageShown(m_book->GetPage(index), /*updateRecent=*/true);
}

void MainNotebook::OnChildFocus(wxChildFocusEvent &event) {
  wxUnusedVar(event);
}

void MainNotebook::OnPageClose(wxAuiNotebookEvent &event) {
  // The page is removed by CloseFile() / CloseSession(), not by the book.
  event.Veto();
  const int index = event.GetSelection();
  if (index == wxNOT_FOUND) {
    return;
  }
  wxWindow *page = m_book->GetPage(index);
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    CloseFile(file);
  } else if (auto *session = dynamic_cast<SessionPage *>(page)) {
    CloseSession(session->GetSession().groupName, session->GetSession().name);
  }
}

void MainNotebook::SendPageEvent(wxEventType type, const SessionRef &ref,
                                 const Account &account,
                                 const SessionRef &oldRef,
                                 PageActivity activity) {
  PageViewEvent event(type, GetId());
  event.SetEventObject(this);
  event.SetRef(ref);
  event.SetOldRef(oldRef);
  event.SetAccount(account);
  event.SetActivity(activity);
  ProcessWindowEvent(event);
}

void MainNotebook::OnShowSessionRequested(PageViewEvent &event) {
  // The user chose a session in the file explorer or the activity view: show
  // it.
  auto *page = FindPage(event.GetRef());
  if (page != nullptr && page != GetCurrentPage()) {
    ShowPage(page);
  }
}

void MainNotebook::OnFileTreeActivated(FileEvent &event) {
  if (event.IsDir()) {
    return; // The tree opens or closes the folder
  }
  const Account &account = event.GetAccount();
  if (account.IsRemote()) {
    OpenRemoteFile(RemoteHostDetails{account.m_name, account.m_user},
                   event.GetPath(), {});
  } else if (account.IsWSL()) {
    OpenLocalFile(wsl::ToWindowsPath(account.m_name, event.GetPath()));
  } else {
    OpenLocalFile(event.GetPath());
  }
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void MainNotebook::AddTab(wxWindow *page) {
  const wxString title = TitleOf(page);
  m_book->AddPage(page, title, /*select=*/false);
  if (const int index = m_book->FindPage(page); index != wxNOT_FOUND) {
    m_book->SetPageBitmap(index, IconOf(page));
    // A fixed width tab can cut the title; the tooltip shows all of it.
    if (auto *filepage = dynamic_cast<FilePage *>(page)) {
      m_book->SetPageToolTip(index, filepage->GetPath());
    } else {
      m_book->SetPageToolTip(index, title);
    }
  }
}

void MainNotebook::DeleteTab(wxWindow *page) {
  if (const int index = m_book->FindPage(page); index != wxNOT_FOUND) {
    m_book->DeletePage(index); // destroys the page window
  }
}

void MainNotebook::DeleteAllTabs() { m_book->DeleteAllPages(); }

size_t MainNotebook::GetPageCount() const { return m_book->GetPageCount(); }

wxWindow *MainNotebook::GetCurrentPage() const {
  return m_book->GetCurrentPage();
}

void MainNotebook::SelectTab(wxWindow *page) {
  if (const int index = m_book->FindPage(page); index != wxNOT_FOUND) {
    m_book->ChangeSelection(index); // sends no event
  }
}

void MainNotebook::SetTabTitle(wxWindow *page, const wxString &title) {
  if (const int index = m_book->FindPage(page); index != wxNOT_FOUND) {
    m_book->SetPageText(index, title);
    m_book->SetPageToolTip(index, title);
  }
}

void MainNotebook::SetStatusIcon(SessionPage *page,
                                 const wxBitmapBundle &status) {
  if (const int index = m_book->FindPage(page); index != wxNOT_FOUND) {
    m_book->SetPageBitmap(index,
                          status.IsOk() ? TabIcon(status) : IconOf(page));
  }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<wxWindow *> MainNotebook::GetPages() const {
  std::vector<wxWindow *> pages;
  for (size_t i = 0; i < m_book->GetPageCount(); ++i) {
    pages.push_back(m_book->GetPage(i));
  }
  return pages;
}

std::vector<SessionPage *> MainNotebook::GetSessions() const {
  std::vector<SessionPage *> sessions;
  for (wxWindow *page : GetPages()) {
    if (auto *session = dynamic_cast<SessionPage *>(page)) {
      sessions.push_back(session);
    }
  }
  return sessions;
}

std::vector<SessionPage *>
MainNotebook::GetSessions(const wxString &group) const {
  std::vector<SessionPage *> sessions;
  for (auto *session : GetSessions()) {
    if (session->GetSession().groupName == group) {
      sessions.push_back(session);
    }
  }
  return sessions;
}

std::vector<FilePage *> MainNotebook::GetFiles() const {
  std::vector<FilePage *> files;
  for (wxWindow *page : GetPages()) {
    if (auto *file = dynamic_cast<FilePage *>(page)) {
      files.push_back(file);
    }
  }
  return files;
}

SessionPage *MainNotebook::FindSession(const wxString &group,
                                       const wxString &name) const {
  for (auto *session : GetSessions(group)) {
    if (session->GetSession().name == name) {
      return session;
    }
  }
  return nullptr;
}

SessionPage *MainNotebook::FindSessionByName(const wxString &name) const {
  for (auto *session : GetSessions()) {
    if (session->GetSession().name == name) {
      return session;
    }
  }
  return nullptr;
}

FilePage *MainNotebook::FindFile(const wxString &key) const {
  for (auto *file : GetFiles()) {
    if (file->GetKey() == key) {
      return file;
    }
  }
  return nullptr;
}

wxWindow *MainNotebook::FindPage(const SessionRef &ref) const {
  if (ref.groupName == kFilesGroupName) {
    return FindFile(ref.name);
  }
  return FindSession(ref.groupName, ref.name);
}

std::optional<SessionRef> MainNotebook::RefOf(wxWindow *page) {
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    return SessionRef{session->GetSession().groupName,
                      session->GetSession().name};
  }
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    return SessionRef{kFilesGroupName, file->GetKey()};
  }
  return std::nullopt;
}

wxArrayString MainNotebook::GetGroupNames() const {
  wxArrayString names;
  names.Add(kDefaultGroupName);
  for (auto *session : GetSessions()) {
    const wxString &group = session->GetSession().groupName;
    if (IsSessionGroupName(group) && names.Index(group) == wxNOT_FOUND) {
      names.Add(group);
    }
  }
  return names;
}

wxWindow *
MainNotebook::GetFallbackPage(std::span<wxWindow *const> closing) const {
  for (const SessionRef &ref : GetPagesByRecency(std::nullopt)) {
    wxWindow *page = FindPage(ref);
    if (std::find(closing.begin(), closing.end(), page) == closing.end()) {
      return page;
    }
  }
  return nullptr;
}

PageSwitcherItem MainNotebook::DescribePage(wxWindow *page) const {
  PageSwitcherItem item;
  if (const int index = m_book->FindPage(page); index != wxNOT_FOUND) {
    item.label = m_book->GetPageText(index);
    item.icon = m_book->GetPageBitmap(index);
  }
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    item.dir = session->GetCurrentDir();
    item.group = session->GetSession().groupName;
  } else if (auto *file = dynamic_cast<FilePage *>(page)) {
    item.dir = file->GetFolder();
    item.group = kFilesGroupName;
  }
  return item;
}

std::vector<SessionRef>
MainNotebook::GetPagesByRecency(const std::optional<SessionRef> &first) const {
  std::vector<SessionRef> remaining;
  for (wxWindow *page : GetPages()) {
    if (auto ref = RefOf(page)) {
      remaining.push_back(*ref);
    }
  }
  std::vector<SessionRef> ordered;
  auto take = [&](const SessionRef &ref) {
    auto it = std::find(remaining.begin(), remaining.end(), ref);
    if (it != remaining.end()) {
      ordered.push_back(std::move(*it));
      remaining.erase(it);
    }
  };

  if (first) {
    take(*first);
  }
  for (const auto &ref : m_recent) {
    take(ref);
  }
  ordered.insert(ordered.end(), std::make_move_iterator(remaining.begin()),
                 std::make_move_iterator(remaining.end()));
  return ordered;
}

void MainNotebook::TouchRecent(const SessionRef &ref) {
  if (!m_recent.empty() && m_recent.front() == ref) {
    return;
  }
  // Drop the closed pages while at it, so the list cannot grow forever.
  std::erase_if(m_recent, [&](const SessionRef &r) {
    return r == ref || FindPage(r) == nullptr;
  });
  m_recent.insert(m_recent.begin(), ref);
  SyncWorkspaceSoon();
}

void MainNotebook::RenameRecent(const SessionRef &from, const SessionRef &to) {
  SendPageEvent(wxEVT_SESSION_PAGE_RENAMED, to, Account::LocalHost(), from);
  std::replace(m_recent.begin(), m_recent.end(), from, to);
}

void MainNotebook::SyncWorkspaceSoon() {
  if (m_syncPending) {
    return;
  }
  m_syncPending = true;
  CallAfter([this] {
    m_syncPending = false;
    SyncWorkspaceToDisk();
  });
}

void MainNotebook::SyncWorkspaceToDisk() {
  Workspace ws;
  ws.version = 1;
  for (auto *page : GetSessions()) {
    Session session = page->GetSession();
    if (session.plainTerminal) {
      // Where the user is now, if it is a folder of this machine (a shell in
      // WSL, for one, reports a path that is not).
      const wxString dir = page->GetCurrentDir();
      if (!dir.empty() && wxDirExists(dir)) {
        session.workingDir = dir;
      }
    }
    ws.sessions.push_back(std::move(session));
  }

  for (auto *file : GetFiles()) {
    const auto &host = file->GetRemoteHostDetails();
    ws.files.push_back(SessionFile{file->GetPath(), host.host, host.user});
  }
  // The remote files that are still coming back from the last run.
  ws.files.insert(ws.files.end(), m_restoringFiles.begin(),
                  m_restoringFiles.end());
  // In the order of the last run (new files last): the remote files come back
  // in the order the fetches finish.
  {
    std::map<wxString, size_t> rank; // The key of a file -> its saved place
    for (const SessionFile &file : AppManager::Get().InitialWorkspace().files) {
      rank.emplace(KeyOfFile(file), rank.size());
    }
    const auto rankOf = [&rank](const SessionFile &file) {
      const auto it = rank.find(KeyOfFile(file));
      return it == rank.end() ? rank.size() : it->second;
    };
    std::stable_sort(ws.files.begin(), ws.files.end(),
                     [&rankOf](const SessionFile &a, const SessionFile &b) {
                       return rankOf(a) < rankOf(b);
                     });
  }

  for (const SessionRef &ref : GetPagesByRecency(std::nullopt)) {
    auto *page = FindPage(ref);
    auto *session = dynamic_cast<SessionPage *>(page);
    if (session && session->GetSession().IsPersistent()) {
      ws.recentSessions.push_back(ref);
    } else if (dynamic_cast<FilePage *>(page) != nullptr) {
      ws.recentSessions.push_back(ref);
    }
  }

  if (Status st = m_workspaceStore->Save(ws); !st.ok()) {
    KLOG_WARN() << "Could not persist workspace.json: " << st.message();
  }
}

// ---------------------------------------------------------------------------
// Showing pages
// ---------------------------------------------------------------------------

void MainNotebook::ShowPage(wxWindow *page, bool updateRecent) {
  // A page that is not (yet or any more) in the notebook is not ours to show.
  if (!IsPage(page)) {
    return;
  }
  SelectTab(page); // sends no event
  PageShown(page, updateRecent);
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    session->CallAfter(&SessionPage::SetFocus);
  } else if (auto *file = dynamic_cast<FilePage *>(page)) {
    file->CallAfter(&FilePage::FocusEditor);
  }
}

bool MainNotebook::IsPage(wxWindow *page) const {
  const auto ref = RefOf(page);
  return ref && FindPage(*ref) == page;
}

void MainNotebook::PageShown(wxWindow *page, bool updateRecent) {
  // The tab is showing: that is what this is told, it does not select it.
  const auto ref = RefOf(page);
  if (!ref || !IsPage(page)) {
    return;
  }

  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    // The user sees the page now: the check mark goes away.
    if (session->GetProgramActivity() == SessionPage::ProgramActivity::Done) {
      session->AcknowledgeDone();
      ApplyPageIcon(session);
    }
    SendPageEvent(wxEVT_SESSION_PAGE_SHOWN, *ref);
    session->ApplyTitle();
    session->PublishStatus();
  } else if (auto *file = dynamic_cast<FilePage *>(page)) {
    // The file is selected in the file explorer, if it is inside the tree that
    // is showing.
    FileEvent reveal(wxEVT_FILE_REVEAL, GetId());
    reveal.SetEventObject(this);
    reveal.SetPath(file->GetAccountPath());
    reveal.SetAccount(file->GetAccount());
    ProcessWindowEvent(reveal);
    wxTheApp->GetTopWindow()->SetLabel(file->GetPath());
    GetMainFrame()->ClearSessionStatus();
    file->PublishStatus();
  }

  if (updateRecent) {
    TouchRecent(*ref);
  }
}

void MainNotebook::SelectFallbackPage() { ShowPage(GetFallbackPage()); }

void MainNotebook::SwitchPage(bool forward) {
  if (m_switcher) {
    m_switcher->Advance(forward);
    return;
  }
  const auto ordered = GetPagesByRecency(GetActivePageRef());
  if (ordered.size() <= 1) {
    return;
  }

  // With Ctrl already released (e.g. picked from the menu) there is nothing to
  // hold on to, so skip the popup.
  if (wxGetMouseState().RawControlDown()) {
    std::vector<PageSwitcherItem> items;
    for (const SessionRef &ref : ordered) {
      items.push_back(DescribePage(FindPage(ref)));
    }
    // Modeless on purpose: a modal loop left the app lagging after closing.
    m_switcherPages = ordered;
    m_switcher =
        new PageSwitcherDlg(wxGetTopLevelParent(this), this, items, forward);
    return;
  }
  const int count = static_cast<int>(ordered.size());
  ShowPage(FindPage(ordered[forward ? 1 : count - 1]));
}

void MainNotebook::OnPageSwitcherDone(wxCommandEvent &event) {
  const int chosen = event.GetInt();
  auto pages = std::move(m_switcherPages);
  m_switcherPages.clear();
  if (m_switcher) {
    m_switcher->Destroy();
    m_switcher = nullptr;
  }
  if (chosen < 0 || chosen >= static_cast<int>(pages.size())) {
    return;
  }
  ShowPage(FindPage(pages[chosen]));
}

// ---------------------------------------------------------------------------
// Adding / removing pages
// ---------------------------------------------------------------------------

SessionPage *MainNotebook::AddSession(SessionPage *page) {
  const Session &session = page->GetSession();
  if (FindSession(session.groupName, session.name) != nullptr) {
    KLOG_WARN() << "A session named '" << session.name
                << "' already exists in group '" << session.groupName << "'";
    return nullptr;
  }
  AddTab(page);
  const SessionRef ref{session.groupName, session.name};
  // Sessions only: a plain terminal has no tree.
  if (!page->IsPlainTerminal()) {
    SendPageEvent(wxEVT_SESSION_PAGE_ADDED, ref, page->GetAccount());
  }
  return page;
}

SessionPage *MainNotebook::AddSessionPage(const Session &session, bool resume) {
  std::optional<AgentDef> agent{std::nullopt};
  if (IsSessionGroupName(session.groupName) && !session.agentName.empty()) {
    auto &registry = AppManager::Get().Adapters();
    const AgentDef *pagent = registry.FindAgent(session.agentName);
    if (pagent == nullptr) {
      KLOG_ERROR() << wxString::Format("No such agent: %s", session.agentName);
      return nullptr;
    }
    agent = *pagent;
  }

  auto *page = new SessionPage(GetPageParent(), agent, session, resume);
  if (page->Status() == SessionStatus::Starting) {
    // Could not start the session
    wxDELETE(page);
    return nullptr;
  }

  if (AddSession(page) == nullptr) {
    wxDELETE(page);
    return nullptr;
  }
  return page;
}

void MainNotebook::AddFilePage(FilePage *page) {
  AddTab(page);
  SyncWorkspaceSoon();
}

void MainNotebook::RemovePage(wxWindow *page) {
  const bool wasActive = (GetCurrentPage() == page);

  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    const wxString &groupName = session->GetSession().groupName;
    SendPageEvent(wxEVT_SESSION_PAGE_REMOVED,
                  SessionRef{groupName, session->GetSession().name});
  }
  // The page used before this one is shown first. Left to the book, it would
  // select the next tab, and that tab would count as the one used last.
  wxWindow *fallback = wasActive ? GetFallbackPage({&page, 1}) : nullptr;
  if (fallback != nullptr) {
    ShowPage(fallback);
  } else if (wasActive) {
    // The status bar showed this page and no other page takes over.
    GetMainFrame()->ClearSessionStatus();
  }
  DeleteTab(page); // destroys the page window
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

void MainNotebook::OnFileSaveStarted(FileEvent &e) {
  auto *page = FindFile(e.GetKey());
  const wxString name = page ? page->GetDisplayName() : e.GetPath();
  GetMainFrame()->SetActivityText(wxString::Format(_("Saving %s"), name));
  GetMainFrame()->StartActivityIndicator();
}

void MainNotebook::OnFileSaveDone(FileEvent &e) {
  GetMainFrame()->StopActivityIndicator();
  GetMainFrame()->ClearActivityText();

  const wxString key = e.GetKey();
  if (m_closeAfterSave.erase(key) > 0 && e.GetStatusCode().ok()) {
    // Deferred: this event is still being dispatched by the very page that is
    // about to be destroyed.
    CallAfter(&MainNotebook::CloseFileByKey, key);
  }
}

void MainNotebook::OpenRemoteFile(const RemoteHostDetails &remoteHost,
                                  const wxString &clickedPath,
                                  const std::vector<wxString> &searchDirs,
                                  int line, int column) {
  KLOG_DEBUG() << "Opening remote file: " << clickedPath;
  const wxString key = FileEvent::MakeKey(clickedPath, remoteHost);
  if (line > 0) {
    m_gotoAfterFetch[key] = FileLocation{clickedPath, line, column};
  }
  if (!m_fetchingRemote.insert(key).second) {
    // A previous click, or the restore of the file, is still
    // connecting/downloading. The file is shown and selected when it arrives,
    // as the user asked for it now.
    // The file stays in m_restoringFiles until the result is in.
    if (IsRestoringFile(key)) {
      m_selectWhenRestored.insert(key);
    }
    m_ignoreRemote.erase(key);
    return;
  }

  // The worker gets copies of the data it needs and never touches the UI. It
  // hands its result back by value through CallAfter(), which runs
  // OnRemoteFileRead() on the UI thread.
  std::thread([this, remoteHost, clickedPath, searchDirs]() {
    auto read = SftpClient::ReadFile(remoteHost.host, remoteHost.user,
                                     clickedPath, searchDirs);

    RemoteReadResult result;
    result.remoteHost = remoteHost;
    result.clickedPath = clickedPath;
    result.ok = read.ok();
    if (read.ok()) {
      result.path = read.value().path;
      result.content = std::move(read.value().content);
    } else {
      result.error = read.status().message();
    }
    CallAfter(&MainNotebook::OnRemoteFileRead, result);
  }).detach();
}

void MainNotebook::ReloadRemoteFile(const RemoteHostDetails &remoteHost,
                                    const wxString &path) {
  const wxString key = FileEvent::MakeKey(path, remoteHost);
  if (m_fetchingRemote.count(key) != 0) {
    return; // Already being fetched.
  }
  m_reloadingRemote.insert(key);
  OpenRemoteFile(remoteHost, path, {});
}

bool MainNotebook::IsRestoringFile(const wxString &key) const {
  return std::any_of(
      m_restoringFiles.begin(), m_restoringFiles.end(),
      [&key](const SessionFile &file) { return KeyOfFile(file) == key; });
}

bool MainNotebook::TakeRestoringFile(const wxString &key) {
  return std::erase_if(m_restoringFiles, [&](const SessionFile &f) {
           return KeyOfFile(f) == key;
         }) != 0;
}

void MainNotebook::OnRemoteFileRead(const RemoteReadResult &result) {
  const wxString fetchKey =
      FileEvent::MakeKey(result.clickedPath, result.remoteHost);
  m_fetchingRemote.erase(fetchKey);
  std::optional<FileLocation> target;
  if (auto node = m_gotoAfterFetch.extract(fetchKey)) {
    target = std::move(node.mapped());
  }
  const bool reload = m_reloadingRemote.erase(fetchKey) != 0;
  if (m_ignoreRemote.erase(fetchKey) != 0) {
    return; // The workspace was cleared since
  }
  // A restore that the user asked for in the meantime is a plain open.
  const bool wanted = m_selectWhenRestored.erase(fetchKey) != 0;
  const bool taken = TakeRestoringFile(fetchKey);
  const bool restoring = taken && !wanted;

  if (!result.ok) {
    KLOG_WARN() << "Remote open failed for '" << result.clickedPath
                << "': " << result.error;
    if (taken) {
      SyncWorkspaceSoon(); // The file is not back: drop it from the workspace
    }
    if (!restoring) {
      ::wxMessageBox(result.error, "Kennel", wxICON_WARNING | wxOK, this);
    }
    return;
  }

  const wxString &path = result.path;
  if (result.content.find('\0') != std::string::npos) {
    if (taken) {
      SyncWorkspaceSoon();
    }
    if (restoring) {
      KLOG_WARN() << path << " is a binary file; not restored";
    } else {
      ::wxMessageBox(wxString::Format(_("%s is a binary file."), path),
                     "Kennel", wxICON_INFORMATION | wxOK, this);
    }
    return;
  }

  // Only a file that decodes as UTF-8 is editable: re-encoding anything else
  // on save would corrupt it, so show that read-only.
  bool editable = true;
  wxString text = wxString::FromUTF8(result.content);
  if (text.empty() && !result.content.empty()) {
    text = wxString::From8BitData(result.content.data(), result.content.size());
    editable = false;
  }
  ShowRemoteFile(result.remoteHost, path, text, editable, reload, restoring);
  if (target) {
    if (auto *page = FindFile(FileEvent::MakeKey(path, result.remoteHost))) {
      page->GotoLine(target->line, target->column);
    }
  }
}

void MainNotebook::OpenLocalFile(const wxString &path, bool restoring) {
  const wxString key = wxFileName(path).GetFullPath();
  if (auto *existing = FindFile(key)) {
    if (!restoring) {
      ShowPage(existing);
    }
    return;
  }

  auto *page = new FilePage(
      GetPageParent(), key,
      ThemeManager::Get().ActiveTheme().value_or(wxTerminalTheme{}));
  if (!page->LoadLocal()) {
    page->Destroy();
    if (restoring) {
      KLOG_WARN() << "Could not restore " << key;
    } else {
      wxMessageBox(wxString::Format(_("Could not open %s"), key), "Kennel",
                   wxOK | wxICON_ERROR, this);
    }
    return;
  }
  AddFilePage(page);
  if (!restoring) {
    ShowPage(page);
  }
}

void MainNotebook::OpenFileAt(const FileLocation &where) {
  OpenLocalFile(where.path);
  if (where.line <= 0) {
    return;
  }
  if (auto *page = FindFile(wxFileName(where.path).GetFullPath())) {
    page->GotoLine(where.line, where.column);
  }
}

void MainNotebook::ShowRemoteFile(const RemoteHostDetails &remoteHost,
                                  const wxString &path, const wxString &text,
                                  bool editable, bool reload, bool restoring) {
  const wxString key = FileEvent::MakeKey(path, remoteHost);
  if (auto *existing = FindFile(key)) {
    // The remote copy may have changed, but only a reload (the user agreed)
    // replaces unsaved edits.
    if (reload || !existing->IsModified()) {
      existing->LoadRemote(text, editable);
    }
    if (!reload && !restoring) {
      ShowPage(existing);
    }
    return;
  }

  auto *page = new FilePage(
      GetPageParent(), path,
      ThemeManager::Get().ActiveTheme().value_or(wxTerminalTheme{}),
      remoteHost);
  page->LoadRemote(text, editable);
  AddFilePage(page);
  if (!restoring) {
    ShowPage(page);
  }
}

void MainNotebook::CloseFile(FilePage *page) {
  CHECK_NOT_NULL_RETURN(page);
  if (page->IsSaving()) {
    return; // Let the upload finish first; closing now would lose the result.
  }
  if (page->IsModified() && page->CanSave()) {
    const int answer = wxMessageBox(
        wxString::Format(_("Save changes to %s?"), page->GetDisplayName()),
        "Kennel", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (answer == wxCANCEL) {
      return;
    }
    if (answer == wxYES) {
      if (page->IsRemote()) {
        // Uploads in the background; OnFileSaveDone closes it once it has
        // gone through.
        if (page->SaveAsync()) {
          m_closeAfterSave.insert(page->GetKey());
        }
        return;
      }
      if (!page->Save()) {
        return;
      }
    }
  }

  m_closeAfterSave.erase(page->GetKey());
  RemovePage(page);
  SyncWorkspaceSoon();
}

void MainNotebook::CloseFileByKey(const wxString &key) {
  if (auto *page = FindFile(key)) {
    CloseFile(page);
  }
}

void MainNotebook::CloseAllFiles() {
  std::vector<wxString> keys;
  for (auto *page : GetFiles()) {
    keys.push_back(page->GetKey());
  }
  for (const wxString &key : keys) {
    CloseFileByKey(key);
  }
}

// ---------------------------------------------------------------------------
// Launching sessions
// ---------------------------------------------------------------------------

void MainNotebook::StartTerminal() {
  // Restored terminals keep their names: skip the ones that are taken.
  static int terminalId{0};
  wxString name;
  do {
    name = wxString::Format(_("Terminal %d"), ++terminalId);
  } while (IsNameExist(name, kTerminalsGroupName));
  const auto &prefs = AppManager::Get().GetPrefs();
  NewSessionRequest request{
      .name = name,
      .agentName = _("Terminals"), // Fake name
      .workingDir = prefs.terminalHomeDir,
      .groupName = kTerminalsGroupName,
      .plainTerminal = true,
  };
  LaunchSession(request);
}

void MainNotebook::RunJob(const JobDef &job, bool selectAfterLaunch) {
  int &sequence = m_jobRunCounters[job.name];
  wxString candidate;
  do {
    candidate = wxString::Format("%s #%d", job.name, ++sequence);
  } while (IsNameExist(candidate, _("Jobs")));

  NewSessionRequest request{
      .name = candidate,
      .groupName = _("Jobs"),
      .jobName = job.name,
  };

  const wxString trigger = selectAfterLaunch ? "manual" : "scheduled";
  const wxString typeStr = JobTypeToString(job.type);
  const wxString typeLabel =
      job.type == JobType::kPrompt ? "Prompt" : "Command";

  if (job.type == JobType::kPrompt) {
    const AgentDef *agent =
        AppManager::Get().Adapters().FindAgent(job.agentName);
    if (agent == nullptr) {
      KLOG_WARN() << "Job '" << job.name << "': agent '" << job.agentName
                  << "' no longer exists; skipping run";
      AppendJobLogEntry(JobLogEntry{
          .event = "failed",
          .job = job.name,
          .type = typeStr,
          .trigger = trigger,
          .reason = wxString::Format("agent '%s' not found", job.agentName),
          .message =
              wxString::Format("Job '%s' failed to start: agent '%s' not found",
                               job.name, job.agentName),
      });
      return;
    }
    request.agentName = job.agentName;
    request.jobCommands =
        BuildJobCommandLine(*agent, wxEmptyString, job.prompt);
  } else {
    request.jobCommands = {job.command};
  }

  if (!job.keepTerminalOpen) {
    request.jobCommands.push_back("exit");
  }

  AppendJobLogEntry(JobLogEntry{
      .event = "start",
      .job = job.name,
      .type = typeStr,
      .trigger = trigger,
      .session = candidate,
      .message = wxString::Format("Job '%s' started (%s, %s)", job.name,
                                  typeLabel, trigger),
  });

  LaunchSession(request, selectAfterLaunch);
}

void MainNotebook::StartAgent(const wxString &agentName,
                              const wxString &groupName) {
  StartAgentDialog dlg(this);
  if (!agentName.empty()) {
    dlg.SetSelectedClientName(agentName);
  }

  wxString selectedGroupName{groupName};
  if (selectedGroupName.empty()) {
    selectedGroupName = GetSelectedGroupName();
  }

  if (!selectedGroupName.empty()) {
    dlg.SetSelectedGroup(selectedGroupName);
  }

  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  LaunchSession(dlg.GetRequest());
}

bool MainNotebook::LaunchSession(const NewSessionRequest &req,
                                 bool selectAfterLaunch) {
  if (req.name.empty()) {
    wxMessageBox("Session name must not be empty", "Launch failed",
                 wxOK | wxICON_ERROR, this);
    return false;
  }
  if (IsNameExist(req.name, req.groupName)) {
    wxMessageBox(
        wxString::Format("A session named '%s' already exists in group '%s'",
                         req.name, req.groupName),
        "Launch failed", wxOK | wxICON_ERROR, this);
    return false;
  }

  Session session;
  session.name = req.name;
  session.agentName = req.agentName;
  session.workingDir = req.workingDir;
  session.groupName = req.groupName;
  session.plainTerminal = req.plainTerminal;
  session.jobCommands = req.jobCommands;
  session.jobName = req.jobName;

  auto *page = AddSessionPage(session, req.resume);
  if (page == nullptr) {
    KLOG_INFO() << "Session creation failed";
    return false;
  }

  if (selectAfterLaunch) {
    ShowPage(page);
  }

  SyncWorkspaceToDisk();

  auto &prefs = AppManager::Get().GetPrefs();
  PushRecent(prefs.recentWorkingDirs, req.workingDir);
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not persist recent working dirs: " << st.message();
  }
  return true;
}

void MainNotebook::RestoreSessions() {
  const auto &initial = AppManager::Get().InitialWorkspace();
  if (initial.sessions.empty() && initial.files.empty()) {
    return;
  }

  int restored = 0;
  for (const Session &s : initial.sessions) {
    auto *page = AddSessionPage(s, true);
    if (page) {
      page->GetTerminal()->EnsureStarted();
      ++restored;
    }
  }
  KLOG_INFO() << "Restored " << restored << " session(s)";

  // The files, in the background: the page used last is shown below.
  for (const SessionFile &file : initial.files) {
    if (file.remoteHost.empty()) {
      OpenLocalFile(file.path, /*restoring=*/true);
    } else {
      m_restoringFiles.push_back(file);
      OpenRemoteFile({file.remoteHost, file.remoteUser}, file.path, {});
    }
  }

  // Bring back the recent order and show the page used last.
  m_recent.clear();
  wxWindow *lastUsed = nullptr;
  for (const SessionRef &r : initial.recentSessions) {
    if (auto *page = FindPage(r)) {
      m_recent.push_back(r);
      if (lastUsed == nullptr) {
        lastUsed = page;
      }
    }
  }
  if (lastUsed != nullptr) {
    ShowPage(lastUsed);
  } else {
    SelectFallbackPage();
  }

  // Anything that failed to restore into the UI (e.g. its agent no longer
  // exists) must not be written back: the UI is the only source of truth (but
  // for the remote files that are still coming back, see m_restoringFiles).
  SyncWorkspaceToDisk();
}

// ---------------------------------------------------------------------------
// Theme, font, prefs
// ---------------------------------------------------------------------------

void MainNotebook::ApplyFont(const wxFont &f) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetFont(f);
  if (!active) {
    return;
  }
  for (auto *page : GetSessions()) {
    page->ApplyTheme(*active);
    for (auto *terminal : page->GetTerminals()) {
      terminal->SendSizeEvent();
    }
  }
  for (auto *filePage : GetFiles()) {
    filePage->ApplyTheme(*active);
  }
  m_book->SendSizeEvent();
  KLOG_INFO() << "Applied terminal font '" << f.GetFaceName() << "' to "
              << static_cast<int>(SessionCount()) << " terminal(s)";
  SavePrefs();
}

void MainNotebook::ApplyOptimizedDrawing() {
  bool optimized = AppManager::Get().GetPrefs().terminalOptimizedDrawing;
  for (auto *page : GetSessions()) {
    for (auto *terminal : page->GetTerminals()) {
      terminal->EnableSafeDrawing(!optimized);
      terminal->Refresh();
    }
  }
}

void MainNotebook::ApplyPrefs() {
  const auto &prefs = AppManager::Get().GetPrefs();
  ApplyTheme(prefs.terminalTheme);
  wxFont font;
  font.SetNativeFontInfo(prefs.terminalFontDesc);
  ApplyFont(font);
  ApplyOptimizedDrawing();

  // The theme of the application may have changed.
  ApplyToolBarStyles();

  for (auto *page : GetSessions()) {
    for (auto *terminal : page->GetTerminals()) {
      terminal->SetBufferSize(prefs.scrollbackLines);
    }
  }
}

void MainNotebook::ApplyToolBarStyles() {
  for (auto *page : GetSessions()) {
    page->ApplyToolBarStyle();
  }
  for (auto *page : GetFiles()) {
    page->ApplyToolBarStyle();
  }
  // The file explorer follows.
  wxCommandEvent style(wxEVT_PAGES_TOOLBAR_STYLE, GetId());
  style.SetEventObject(this);
  ProcessWindowEvent(style);
}

void MainNotebook::OnThemeApplied(wxCommandEvent &event) {
  wxUnusedVar(event);
  // A new art for each theme: the art does not follow the system colours.
  auto theme = ThemeManager::Get().ActiveTheme().value_or(
      wxSystemSettings::GetAppearance().IsDark()
          ? wxTerminalTheme::MakeDarkTheme()
          : wxTerminalTheme::MakeLightTheme());
  auto art = new GlyphTabArt(theme);
  const auto &prefs = AppManager::Get().GetPrefs();
  art->SetOutlineColour(
      SettingsDlg::GetTabOutlineColourOf(theme, prefs.tabOutlineColour));
  m_book->SetBackgroundColour(theme.bg);
  m_book->SetArtProvider(art);
  m_book->SetTabPosition(prefs.tabPosition);
  m_book->Refresh();
}

void MainNotebook::ApplyTheme(const wxString &themeName) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetTheme(themeName);
  if (!active) {
    return;
  }
  for (auto *page : GetSessions()) {
    page->ApplyTheme(*active);
    for (auto *terminal : page->GetTerminals()) {
      terminal->SendSizeEvent();
    }
  }
  for (auto *filePage : GetFiles()) {
    filePage->ApplyTheme(*active);
  }
  m_book->SendSizeEvent();
  SavePrefs();

  // The application looks like the terminal theme: light or dark with it.
  if (AppManager::Get().GetPrefs().appTheme == kAppThemeFollowTerminal) {
    ApplyAppTheme(kAppThemeFollowTerminal);
    ApplyToolBarStyles();
  }
}

void MainNotebook::SavePrefs() {
  auto &themeMgr = ThemeManager::Get();
  auto &prefs = AppManager::Get().GetPrefs();
  prefs.terminalTheme = themeMgr.CurrentThemeName();
  if (const auto theme = themeMgr.ActiveTheme(); theme && theme->font.IsOk()) {
    prefs.terminalFontDesc = theme->font.GetNativeFontInfoDesc();
  }

  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not persist UI prefs: " << st.message();
  }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
bool MainNotebook::IsEmpty() const { return GetPageCount() == 0; }

wxString MainNotebook::GetSelectedGroupName() const {
  // The group of the session that is showing (a file has none).
  auto *session = GetActiveSessionPage();
  return session ? session->GetSession().groupName : wxString{};
}

SessionPage *MainNotebook::GetActiveSessionPage() const {
  return dynamic_cast<SessionPage *>(GetCurrentPage());
}

std::optional<SessionRef> MainNotebook::GetActivePageRef() const {
  return RefOf(GetCurrentPage());
}

bool MainNotebook::IsNameExist(const wxString &name,
                               const wxString &groupName) const {
  return FindSession(groupName, name) != nullptr;
}

size_t MainNotebook::SessionCount() const { return GetSessions().size(); }

size_t MainNotebook::PageCount() const { return GetPageCount(); }

bool MainNotebook::CanRefreshCurrent() const {
  auto *page = GetActiveSessionPage();
  return page != nullptr && IsSessionGroupName(page->GetSession().groupName);
}

void MainNotebook::GotoLine() {
  auto *file = dynamic_cast<FilePage *>(GetCurrentPage());
  if (file == nullptr) {
    return;
  }
  if (!m_gotoBar->IsShown()) {
    // Not when it is open already (Ctrl+G in the box): the caret has followed
    // the text by now, and Escape must go back to where the user was.
    m_gotoFileKey = file->GetKey();
    m_gotoView = file->GetViewState();
    m_gotoBar->Show();
    Layout();
  }
  m_gotoBar->SetInitialText(":");
}

bool MainNotebook::CanGotoLine() const {
  return dynamic_cast<FilePage *>(GetCurrentPage()) != nullptr;
}

void MainNotebook::OnGotoChanged(wxCommandEvent &event) {
  // ":12" or ":12:5": the caret follows as the user types.
  const wxString text = event.GetString();
  auto *file = dynamic_cast<FilePage *>(GetCurrentPage());
  int line = 0;
  int column = 0;
  if (file != nullptr && text.StartsWith(":") &&
      ParseLineColumn(text.Mid(1), &line, &column)) {
    file->GotoLine(line, std::max(column, 1));
  }
}

void MainNotebook::OnGotoClosed(wxCommandEvent &event) {
  // The bar has hidden itself already: take its place back, whatever the page.
  Layout();
  auto *file = dynamic_cast<FilePage *>(GetCurrentPage());
  if (file == nullptr) {
    return;
  }
  switch (event.GetInt()) {
  case GotoAnythingBar::kCancelled:
    if (file->GetKey() == m_gotoFileKey) {
      file->SetViewState(m_gotoView); // As it was before the box opened
    }
    file->FocusEditor();
    break;
  case GotoAnythingBar::kAccepted:
    OnGotoChanged(event); // The last text, if it was not shown yet
    file->FocusEditor();
    break;
  default:
    break; // The user clicked somewhere: leave the caret and the focus
  }
}

// ---------------------------------------------------------------------------
// Session / group actions
// ---------------------------------------------------------------------------

void MainNotebook::RefreshCurrentSelection() {
  auto *page = GetActiveSessionPage();
  if (page != nullptr && IsSessionGroupName(page->GetSession().groupName)) {
    page->Restart();
  }
}

void MainNotebook::RefreshGroup(const wxString &groupName) {
  if (!IsSessionGroupName(groupName)) {
    return;
  }
  for (auto *page : GetSessions(groupName)) {
    page->CallAfter(&SessionPage::Restart);
    m_pendingIdle++;
  }
  if (m_pendingIdle > 0) {
    GetMainFrame()->SetActivityText(
        wxString::Format(_("Refreshing %d sessions"), m_pendingIdle));
    GetMainFrame()->StartActivityIndicator();
  }
}

void MainNotebook::CloseAllSessions() {
  if (SessionCount() == 0) {
    return;
  }
  wxString msg;
  msg << _("This operation will close ALL sessions.\nContinue?");
  if (wxMessageBox(msg, "Kennel",
                   wxICON_WARNING | wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT) !=
      wxYES) {
    return;
  }
  CallAfter(&MainNotebook::DeleteAll);
}

void MainNotebook::DeleteAll() {
  m_closeAfterSave.clear();
  for (const SessionFile &file : m_restoringFiles) {
    m_ignoreRemote.insert(KeyOfFile(file));
  }
  m_restoringFiles.clear();
  m_selectWhenRestored.clear();
  DeleteAllTabs();
  wxCommandEvent cleared(wxEVT_SESSION_PAGES_CLEARED, GetId());
  cleared.SetEventObject(this);
  ProcessWindowEvent(cleared);
  m_recent.clear();
  GetMainFrame()->ClearSessionStatus();
  // A page destroyed mid-save never delivers wxEVT_FILE_SAVE_DONE.
  GetMainFrame()->StopActivityIndicator();
  GetMainFrame()->ClearActivityText();
  SyncWorkspaceToDisk();
}

void MainNotebook::DeleteGroupByName(const wxString &name) {
  auto sessions = GetSessions(name);
  if (!sessions.empty()) {
    wxString msg;
    msg << _("This will close ") << sessions.size()
        << _(" session(s).\nContinue?");
    if (wxMessageBox(msg, "Kennel",
                     wxICON_WARNING | wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT) !=
        wxYES) {
      return;
    }
  }

  // When the page that is showing goes away, the page used before it is shown
  // first (see RemovePage()).
  const std::vector<wxWindow *> closing(sessions.begin(), sessions.end());
  const bool activeDeleted = std::find(closing.begin(), closing.end(),
                                       GetCurrentPage()) != closing.end();
  wxWindow *fallback = activeDeleted ? GetFallbackPage(closing) : nullptr;
  if (fallback != nullptr) {
    ShowPage(fallback);
  } else if (activeDeleted) {
    GetMainFrame()->ClearSessionStatus();
  }
  for (auto *page : sessions) {
    SendPageEvent(
        wxEVT_SESSION_PAGE_REMOVED,
        SessionRef{page->GetSession().groupName, page->GetSession().name});
    DeleteTab(page);
  }

  SyncWorkspaceToDisk();

  if (GetPageCount() == 0) {
    wxTheApp->GetTopWindow()->SetLabel(_("Kennel"));
  }
}

void MainNotebook::RenameGroup(const wxString &oldName) {
  // The "Default" group must always exist and cannot be renamed.
  if (oldName == kDefaultGroupName) {
    return;
  }

  wxString newName =
      ::wxGetTextFromUser(_("Choose new group name:"), "Kennel", oldName, this);
  if (newName.empty() || newName == oldName) {
    return;
  }

  if (IsReservedGroupName(newName)) {
    wxMessageBox(wxString::Format(_("'%s' is a reserved name"), newName),
                 "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }

  if (GetGroupNames().Index(newName) != wxNOT_FOUND) {
    wxMessageBox(
        wxString::Format(_("A group named '%s' already exists"), newName),
        "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }

  for (auto *page : GetSessions(oldName)) {
    RenameRecent(SessionRef{oldName, page->GetSession().name},
                 SessionRef{newName, page->GetSession().name});
    page->GetSession().groupName = newName;
  }
  SyncWorkspaceToDisk();
}

void MainNotebook::DuplicateSession(SessionPage *page) {
  CHECK_NOT_NULL_RETURN(page);
  const auto &session = page->GetSession();

  wxString candidate;
  int suffix = 1;
  do {
    candidate = wxString::Format("%s%d", session.name, suffix++);
  } while (IsNameExist(candidate, session.groupName));

  StartAgentDialog dlg(this);
  dlg.SetSelectedClientName(session.agentName);
  dlg.SetSelectedGroup(session.groupName);
  dlg.SetSessionName(candidate);

  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  LaunchSession(dlg.GetRequest());
}

void MainNotebook::RenameSession(SessionPage *page) {
  CHECK_NOT_NULL_RETURN(page);
  const wxString oldName = page->GetSession().name;
  wxString newName =
      ::wxGetTextFromUser(_("New name:"), "Kennel", oldName, this);
  if (newName.empty() || newName == oldName) {
    return;
  }

  if (IsNameExist(newName, page->GetSession().groupName)) {
    wxMessageBox(_("A session with this name already exists in this group"),
                 "Kennel", wxICON_WARNING | wxOK | wxCENTER, this);
    return;
  }

  RenameRecent(SessionRef{page->GetSession().groupName, oldName},
               SessionRef{page->GetSession().groupName, newName});
  page->GetSession().name = newName;
  page->SetDefaultSessionName(newName);
  SetTabTitle(page, newName);
  SyncWorkspaceToDisk();
}

void MainNotebook::RenameItem() {
  if (auto *session = GetActiveSessionPage()) {
    RenameSession(session);
  }
}

void MainNotebook::CloseSessionByName(const wxString &sessionName) {
  if (auto *page = FindSessionByName(sessionName)) {
    CloseSession(page->GetSession().groupName, sessionName);
  }
}

void MainNotebook::CloseSession(const wxString &groupName,
                                const wxString &sessionName) {
  auto *page = FindSession(groupName, sessionName);
  CHECK_NOT_NULL_RETURN(page);
  RemovePage(page);
  SyncWorkspaceToDisk();
}

void MainNotebook::MoveSessionToGroup(const wxString &sessionName,
                                      const wxString &fromGroupName,
                                      const wxString &toGroupName) {
  KLOG_DEBUG() << "Moving session: " << sessionName
               << " from: " << fromGroupName << "->" << toGroupName;

  auto *page = FindSession(fromGroupName, sessionName);
  CHECK_NOT_NULL_RETURN(page);

  if (IsNameExist(sessionName, toGroupName)) {
    wxMessageBox(
        wxString::Format(_("Cannot move session '%s' to group '%s': a session "
                           "with that name already exists there."),
                         sessionName, toGroupName),
        "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }

  const bool wasActive = (GetActiveSessionPage() == page);
  RenameRecent(SessionRef{fromGroupName, sessionName},
               SessionRef{toGroupName, sessionName});
  page->GetSession().groupName = toGroupName;
  ApplyPageIcon(page);

  SyncWorkspaceToDisk();

  if (wasActive) {
    ShowPage(page);
  }
}

void MainNotebook::OnSessionIdle(wxCommandEvent &e) {
  e.Skip();
  if (m_pendingIdle > 0) {
    m_pendingIdle--;
  }

  if (m_pendingIdle == 0) {
    GetMainFrame()->StopActivityIndicator();
    GetMainFrame()->ClearActivityText();
  } else if (m_pendingIdle > 0) {
    GetMainFrame()->SetActivityText(
        wxString::Format(_("Refreshing %d sessions"), m_pendingIdle));
  }
}

void MainNotebook::OnSessionActive(wxCommandEvent &e) { e.Skip(); }

void MainNotebook::OnSessionCwd(wxCommandEvent &e) {
  e.Skip();
  // A restored terminal starts in the folder it was left in. Agents are
  // restored in their launch folder: nothing to save for them.
  // The event is pending: the page may be gone. Compare the pointer only.
  for (auto *page : GetSessions()) {
    if (page == e.GetEventObject()) {
      if (page->GetSession().plainTerminal) {
        SyncWorkspaceSoon();
      }
      break;
    }
  }
}

void MainNotebook::OnSessionProgram(wxCommandEvent &e) {
  e.Skip();
  // First: the spinner has its frames when a tab gets one.
  UpdateSpinnerTimer();
  // The event is pending: the page may be gone. Compare the pointer only.
  for (auto *page : GetSessions()) {
    if (page == e.GetEventObject()) {
      // Before the check mark of the page that is showing goes away: a done of
      // the page that is showing is reported on purpose, and the activity view
      // removes it when the application is idle (ActivityView::OnIdle()).
      const Session &session = page->GetSession();
      SendPageEvent(wxEVT_SESSION_PAGE_ACTIVITY,
                    SessionRef{session.groupName, session.name},
                    page->GetAccount(), {}, page->GetProgramActivity());
      // Nobody needs a check mark on the page that is showing.
      if (page == GetActiveSessionPage()) {
        page->AcknowledgeDone();
      }
      ApplyPageIcon(page);
      break;
    }
  }
}

void MainNotebook::OnSpinnerFrame() {
  bool working = false;
  for (auto *page : GetSessions()) {
    if (page->GetProgramActivity() == SessionPage::ProgramActivity::Working) {
      working = true;
      ApplyPageIcon(page);
    }
  }
  if (!working) {
    m_spinner.Stop();
  }
}

void MainNotebook::UpdateSpinnerTimer() {
  for (auto *page : GetSessions()) {
    if (page->GetProgramActivity() == SessionPage::ProgramActivity::Working) {
      m_spinner.Start(); // It stops itself when nobody is working any more.
      return;
    }
  }
}

void MainNotebook::ApplyPageIcon(SessionPage *page) {
  // Not ok when the program does nothing.
  wxBitmapBundle status;
  switch (page->GetProgramActivity()) {
  case SessionPage::ProgramActivity::Working:
    status = m_spinner.GetBitmap();
    break;
  case SessionPage::ProgramActivity::Done:
    status = m_checkIcon;
    break;
  case SessionPage::ProgramActivity::Blocked:
    // The program needs the user: it raises its hand.
    status = m_handIcon;
    break;
  case SessionPage::ProgramActivity::None:
    break;
  }
  SetStatusIcon(page, status);
}

void MainNotebook::OnSessionExited(wxCommandEvent &e) {
  const wxString &sessionName = e.GetString();
  if (auto *page = FindSessionByName(sessionName)) {
    const Session &session = page->GetSession();
    if (session.IsJobRun()) {
      AppendJobLogEntry(JobLogEntry{
          .event = "end",
          .job = session.jobName,
          .session = sessionName,
          .message =
              wxString::Format("Job '%s' terminal closed", session.jobName),
      });
    }
  }

  // Deferred: removing the session (and its now-empty group, if any)
  // synchronously from inside this handler would destroy the page that is
  // sending the event.
  CallAfter(&MainNotebook::CloseSessionByName, e.GetString());
}

void MainNotebook::OnIdleEvent(wxIdleEvent &e) {
  if (!m_idleHandled && GetActiveSessionPage()) {
    m_idleHandled = true;
    GetActiveSessionPage()->SetFocus();
  }
}

// ---------------------------------------------------------------------------
// Find
// ---------------------------------------------------------------------------

void MainNotebook::ShowFind() {
  wxWindow *page = GetCurrentPage();
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    file->ShowFind();
  } else if (auto *session = dynamic_cast<SessionPage *>(page)) {
    session->ShowFind();
  }
}

void MainNotebook::FindNext(bool forward) {
  wxWindow *page = GetCurrentPage();
  if (auto *file = dynamic_cast<FilePage *>(page)) {
    file->FindNext(forward);
  } else if (auto *session = dynamic_cast<SessionPage *>(page)) {
    session->FindNext(forward);
  }
}

bool MainNotebook::CanFind() const {
  wxWindow *page = GetCurrentPage();
  return dynamic_cast<FilePage *>(page) != nullptr ||
         dynamic_cast<SessionPage *>(page) != nullptr;
}

bool MainNotebook::CanFindNext() const {
  wxWindow *page = GetCurrentPage();
  if (dynamic_cast<FilePage *>(page) != nullptr) {
    return true;
  }
  auto *session = dynamic_cast<SessionPage *>(page);
  return session != nullptr && session->HasSearch();
}
