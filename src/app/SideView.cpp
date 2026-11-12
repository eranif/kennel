#include "app/SideView.hpp"

#include "GlyphBook.h"
#include "GlyphTabPainter.h"
#include "ThemeManager.h"
#include "app/GitPanel.hpp"
#include "app/PageNames.h"
#include "app/SettingsDlg.hpp"
#include "app/TabIcon.h"
#include "app/TabPadding.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/GitDriver.h"
#include "core/Logger.h"
#include "core/WslPath.h"

#include <algorithm>
#include <atomic>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/wupdlock.h>

SideView::SideView(wxWindow *parent) : SideViewBase(parent) {
  m_explorerBook = new wxSimplebook(GetPanelFileExplorer());
  GetPanelFileExplorer()->GetSizer()->Add(m_explorerBook,
                                          wxSizerFlags(1).Expand());
  m_gitBook = new wxSimplebook(GetPanelGit());
  GetPanelGit()->GetSizer()->Add(m_gitBook, wxSizerFlags(1).Expand());
  if (const int page = m_auiSideBook->FindPage(GetBookPageGit());
      page != wxNOT_FOUND) {
    m_auiSideBook->SetPageBitmap(
        page, TabIcon(AppManager::Get().GetBitmaps().GetByAlias("git", false)));
  }
  m_auiSideBook->GetTabBar()->SetCursor(wxCURSOR_HAND);

  auto *notifier = EventNotifier::Get();
  notifier->Bind(wxEVT_SESSION_PAGE_ADDED, &SideView::OnSessionPageAdded, this);
  notifier->Bind(wxEVT_SESSION_PAGE_REMOVED, &SideView::OnSessionPageRemoved,
                 this);
  notifier->Bind(wxEVT_SESSION_PAGE_RENAMED, &SideView::OnSessionRenamed, this);
  notifier->Bind(wxEVT_SESSION_PAGE_SHOWN, &SideView::OnSessionPageShown, this);
  notifier->Bind(wxEVT_SESSION_PAGES_CLEARED, &SideView::OnSessionPagesCleared,
                 this);
  notifier->Bind(wxEVT_SESSION_PAGE_ACTIVITY, &SideView::OnSessionPageActivity,
                 this);
  notifier->Bind(wxEVT_SESSION_PAGE_MOVED, &SideView::OnSessionPageMoved, this);
  notifier->Bind(wxEVT_FILETREE_SET_ROOT, &SideView::OnChangeFolder, this);
  notifier->Bind(wxEVT_GITVIEW_CHANGE_FOLDER, &SideView::OnChangeFolder, this);
  notifier->Bind(wxEVT_FILE_SAVED, &SideView::OnFileSaved, this);
  notifier->Bind(wxEVT_FILE_REVEAL, &SideView::OnFileReveal, this);
  // The remote trees answer on the notifier (see ShowContext()).
  notifier->Bind(wxEVT_FILETREE_CONNECTED, &SideView::OnTreeConnected, this);
  notifier->Bind(wxEVT_APP_TAB_HEIGHT_PREVIEW, &SideView::OnTabHeightPreview,
                 this);
}

SideView::~SideView() {
  auto *notifier = EventNotifier::Get();
  notifier->Unbind(wxEVT_SESSION_PAGE_ADDED, &SideView::OnSessionPageAdded,
                   this);
  notifier->Unbind(wxEVT_SESSION_PAGE_REMOVED, &SideView::OnSessionPageRemoved,
                   this);
  notifier->Unbind(wxEVT_SESSION_PAGE_RENAMED, &SideView::OnSessionRenamed,
                   this);
  notifier->Unbind(wxEVT_SESSION_PAGE_SHOWN, &SideView::OnSessionPageShown,
                   this);
  notifier->Unbind(wxEVT_SESSION_PAGES_CLEARED,
                   &SideView::OnSessionPagesCleared, this);
  notifier->Unbind(wxEVT_SESSION_PAGE_ACTIVITY,
                   &SideView::OnSessionPageActivity, this);
  notifier->Unbind(wxEVT_SESSION_PAGE_MOVED, &SideView::OnSessionPageMoved,
                   this);
  notifier->Unbind(wxEVT_FILETREE_SET_ROOT, &SideView::OnChangeFolder, this);
  notifier->Unbind(wxEVT_GITVIEW_CHANGE_FOLDER, &SideView::OnChangeFolder,
                   this);
  notifier->Unbind(wxEVT_FILE_SAVED, &SideView::OnFileSaved, this);
  notifier->Unbind(wxEVT_FILE_REVEAL, &SideView::OnFileReveal, this);
  notifier->Unbind(wxEVT_FILETREE_CONNECTED, &SideView::OnTreeConnected, this);
  notifier->Unbind(wxEVT_APP_TAB_HEIGHT_PREVIEW, &SideView::OnTabHeightPreview,
                   this);
  // The drivers of the contexts that are left end here, with the others.
  for (auto &[key, context] : m_contexts) {
    ReapDriver(context.git->ReleaseDriver());
  }
  JoinReapers();
}

void SideView::ReapDriver(std::unique_ptr<GitDriver> driver) {
  // A driver ends by waiting for the git that runs, which can take seconds (an
  // ssh host that does not answer): that is done on another thread, the UI goes
  // on. The destructor of the view waits for the threads that are left.
  if (!driver) {
    return;
  }
  // The threads that are done are joined now (a terminal that changes folder
  // ends a driver each time): the list does not grow with the session.
  std::erase_if(m_reapers, [](Reaper &reaper) {
    if (!reaper.done->load()) {
      return false;
    }
    reaper.thread.join();
    return true;
  });
  auto done = std::make_shared<std::atomic<bool>>(false);
  m_reapers.push_back(
      {std::thread([driver = std::move(driver), done]() mutable {
         driver.reset();
         *done = true;
       }),
       done});
}

void SideView::JoinReapers() {
  for (Reaper &reaper : m_reapers) {
    reaper.thread.join();
  }
  m_reapers.clear();
}

// ---------------------------------------------------------------------------
// The tabs
// ---------------------------------------------------------------------------

int SideView::GetSelection() const { return m_auiSideBook->GetSelection(); }

void SideView::SetSelection(int index) {
  if (index >= 0 && index < static_cast<int>(m_auiSideBook->GetPageCount())) {
    m_auiSideBook->SetSelection(index);
  }
}

void SideView::ApplyPrefs() {
  const auto &prefs = AppManager::Get().GetPrefs();
  auto theme = ThemeManager::Get().ActiveTheme().value_or(
      wxSystemSettings::GetAppearance().IsDark()
          ? wxTerminalTheme::MakeDarkTheme()
          : wxTerminalTheme::MakeLightTheme());
  m_auiSideBook->SetTheme(
      theme, SettingsDlg::GetTabOutlineColourOf(theme, prefs.tabOutlineColour),
      GlyphTabPainter::Style::Browser);
  // Top or bottom: a vertical tab bar would take the width of the side bar.
  m_auiSideBook->SetTabPosition(prefs.sideBarTabPosition == wxBOTTOM ? wxBOTTOM
                                                                     : wxTOP);
  m_auiSideBook->SetTabPaddingY(TabPaddingOf(prefs.tabHeight));
  m_auiSideBook->Refresh();
}

void SideView::OnTabHeightPreview(wxCommandEvent &event) {
  event.Skip(); // The notebook shows it too
  m_auiSideBook->SetTabPaddingY(
      TabPaddingOf(static_cast<TabHeight>(event.GetInt())));
  m_auiSideBook->Refresh();
}

void SideView::ApplyToolBarStyle() {
  for (auto &[key, context] : m_contexts) {
    context.git->ApplyToolBarStyle();
  }
}

// ---------------------------------------------------------------------------
// The contexts
// ---------------------------------------------------------------------------

wxString SideView::KeyOf(const Account &account) {
  return account.GetId() + "|" + account.m_rootDir;
}

SideView::Context *SideView::FindContext(const SessionRef &ref) {
  for (auto &[key, context] : m_contexts) {
    if (std::find(context.sessions.begin(), context.sessions.end(), ref) !=
        context.sessions.end()) {
      return &context;
    }
  }
  return nullptr;
}

SideView::Context *SideView::ShownContext() {
  const wxWindow *shown = m_explorerBook->GetCurrentPage();
  for (auto &[key, context] : m_contexts) {
    if (context.tree == shown) {
      return &context;
    }
  }
  return nullptr;
}

void SideView::AddSession(const SessionRef &ref, const Account &account) {
  if (FindContext(ref) != nullptr) {
    return;
  }
  // A terminal has a context of its own; the others share the place.
  const bool isTerminal = ref.groupName == kTerminalsGroupName;
  const wxString key = isTerminal
                           ? wxString::Format("terminal#%d", ++m_nextTerminalId)
                           : KeyOf(account);
  // `isNew` is always true for a terminal (its key is new).
  auto [it, isNew] = m_contexts.try_emplace(key);
  Context &context = it->second;
  if (isNew) {
    context.account = account;
    context.isTerminal = isTerminal;
    // A remote tree is connected when it is shown for the first time: see
    // ShowContext().
    context.tree = new FileTreeCtrl(m_explorerBook, account);
    m_explorerBook->AddPage(context.tree, account.GetDisplayName(), false);
    context.git = new GitPanel(m_gitBook, account);
    m_gitBook->AddPage(context.git, account.GetDisplayName(), false);
  }
  context.sessions.push_back(ref);

  // The first context is shown, and so is the one of the session the user is
  // working in.
  if ((isNew && m_contexts.size() == 1) || m_active == ref) {
    ShowContext(context);
  }
}

void SideView::RemoveSession(const SessionRef &ref) {
  if (m_active == ref) {
    m_active.reset();
  }
  for (auto &[key, context] : m_contexts) {
    auto &sessions = context.sessions;
    const auto at = std::find(sessions.begin(), sessions.end(), ref);
    if (at == sessions.end()) {
      continue;
    }
    sessions.erase(at);
    if (sessions.empty()) {
      DeleteContext(key);
    }
    return;
  }
}

void SideView::RenameSession(const SessionRef &from, const SessionRef &to) {
  if (Context *context = FindContext(from)) {
    std::replace(context->sessions.begin(), context->sessions.end(), from, to);
  }
  if (m_active == from) {
    m_active = to;
  }
}

void SideView::DeleteContext(const wxString &key, bool reshow) {
  const auto it = m_contexts.find(key);
  if (it == m_contexts.end()) {
    return;
  }
  Context &context = it->second;
  const bool wasShown = ShownContext() == &context;
  ReapDriver(context.git->ReleaseDriver());
  // The answer of a connection that is still made is dropped (no context has
  // its request any more).
  if (const int page = m_explorerBook->FindPage(context.tree);
      page != wxNOT_FOUND) {
    m_explorerBook->DeletePage(page);
  }
  if (const int page = m_gitBook->FindPage(context.git); page != wxNOT_FOUND) {
    m_gitBook->DeletePage(page);
  }
  m_contexts.erase(it);
  if (!wasShown || !reshow) {
    return;
  }
  // The book has no page selected now. The session the user works in has the
  // say; the next wxEVT_SESSION_PAGE_SHOWN is not sent when a file page takes
  // over, so the side bar must not be left blank. Without that session, any
  // context that is safe to show: one on this machine or WSL, or a remote one
  // that is connected already. A new connection is never started here.
  const auto isSafe = [](const Context &candidate) {
    return !candidate.account.IsRemote() || candidate.connectStarted;
  };
  Context *next = m_active ? FindContext(*m_active) : nullptr;
  if (next != nullptr && !isSafe(*next)) {
    next = nullptr;
  }
  if (next == nullptr) {
    for (auto &[unused, candidate] : m_contexts) {
      if (isSafe(candidate)) {
        next = &candidate;
        break;
      }
    }
  }
  if (next != nullptr) {
    ShowContext(*next);
  }
}

void SideView::Clear() {
  // Everything goes: nothing is shown in between.
  while (!m_contexts.empty()) {
    DeleteContext(m_contexts.begin()->first, /*reshow=*/false);
  }
  m_active.reset();
}

void SideView::ShowContext(Context &context) {
  {
    // Frozen for the two switches only: not for the work below.
    wxWindowUpdateLocker locker{this};
    if (const int page = m_explorerBook->FindPage(context.tree);
        page != wxNOT_FOUND && page != m_explorerBook->GetSelection()) {
      m_explorerBook->SetSelection(page);
    }
    if (const int page = m_gitBook->FindPage(context.git);
        page != wxNOT_FOUND && page != m_gitBook->GetSelection()) {
      m_gitBook->SetSelection(page);
    }
  }
  // Shows what the panel has now, and the answer comes later.
  context.git->Activate();
  if (context.account.IsRemote() && !context.connectStarted) {
    context.connectStarted = true;
    context.connectRequestId = context.tree->ConnectAsync();
  }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void SideView::OnSessionPageAdded(PageViewEvent &event) {
  event.Skip();
  AddSession(event.GetRef(), event.GetAccount());
}

void SideView::OnSessionPageRemoved(PageViewEvent &event) {
  event.Skip();
  RemoveSession(event.GetRef());
}

void SideView::OnSessionRenamed(PageViewEvent &event) {
  event.Skip();
  RenameSession(event.GetOldRef(), event.GetRef());
}

void SideView::OnSessionPageShown(PageViewEvent &event) {
  event.Skip();
  m_active = event.GetRef();
  if (Context *context = FindContext(*m_active)) {
    ShowContext(*context);
  }
}

// A terminal went to another folder: its tree and its git panel go there.
void SideView::OnSessionPageMoved(PageViewEvent &event) {
  event.Skip();
  Context *context = FindContext(event.GetRef());
  if (context == nullptr || !context->isTerminal) {
    return;
  }
  const Account &account = event.GetAccount();
  if (account.GetId() != context->account.GetId() ||
      account.m_rootDir == context->account.m_rootDir) {
    return; // Not the same shell, or not another folder
  }
  // A shell with no known folder keeps the folder it had: the account, the
  // tree and the git panel stay as they are.
  if (account.m_rootDir.empty()) {
    return;
  }
  context->account = account;
  if (!account.IsRemote()) {
    context->tree->SetRoot(account.m_rootDir);
  }
  ReapDriver(context->git->SetRootDir(account.m_rootDir));
  if (ShownContext() == context) {
    context->git->Activate();
  }
}

void SideView::OnChangeFolder(FileEvent &event) {
  event.Skip();
  // The context of the tree or the git panel that sent it (it may be gone by
  // now).
  for (const auto &[key, context] : m_contexts) {
    if (context.tree == event.GetEventObject() ||
        context.git == event.GetEventObject()) {
      ChangeFolder(key, event.GetPath());
      return;
    }
  }
}

void SideView::ChangeFolder(const wxString &key, const wxString &dir) {
  const auto it = m_contexts.find(key);
  if (it == m_contexts.end() || it->second.account.m_rootDir == dir) {
    return;
  }
  Context &context = it->second;
  Account account = context.account;
  account.m_rootDir = dir;

  // The session that moves: the only one, or the one the user works in.
  const bool single = context.sessions.size() <= 1;
  std::optional<SessionRef> moving;
  if (single && !context.sessions.empty()) {
    moving = context.sessions.front();
  } else if (m_active && FindContext(*m_active) == &context) {
    moving = m_active;
  }
  if (!moving) {
    return;
  }
  const wxString newKey = wxString::Format("folder#%d", ++m_nextTerminalId);
  // The page keeps the choice (it is saved). A terminal goes where its shell
  // goes.
  if (!context.isTerminal) {
    PageViewEvent saved(wxEVT_SESSION_SIDE_DIR_CHANGED);
    saved.SetEventObject(this);
    saved.SetRef(*moving);
    saved.SetAccount(account);
    EventNotifier::Get()->ProcessEvent(saved);
  }

  // One session: the tree and the panel go to the folder. A remote tree is
  // reused only when it is connected: while it connects it must not be used.
  const bool usable = !account.IsRemote() ||
                      (context.connectStarted && context.connectRequestId == 0);
  if (single && usable) {
    context.account = account;
    context.tree->SetRoot(dir);
    ReapDriver(context.git->SetRootDir(dir));
    if (ShownContext() == &context) {
      context.git->Activate();
    }
    if (!context.isTerminal) {
      // The key tells the place of the sessions that share a context: this one
      // is not at the folder of its session any more.
      auto node = m_contexts.extract(it);
      node.key() = newKey;
      m_contexts.insert(std::move(node));
    }
    return;
  }

  // Several sessions share the place: the session that moves gets a context of
  // its own, new tree and new panel.
  Context &moved = m_contexts.try_emplace(newKey).first->second;
  moved.account = account;
  moved.isTerminal = context.isTerminal;
  moved.tree = new FileTreeCtrl(m_explorerBook, account);
  m_explorerBook->AddPage(moved.tree, account.GetDisplayName(), false);
  moved.git = new GitPanel(m_gitBook, account);
  m_gitBook->AddPage(moved.git, account.GetDisplayName(), false);
  moved.sessions.push_back(*moving);

  auto &sessions = context.sessions;
  sessions.erase(std::find(sessions.begin(), sessions.end(), *moving));
  const bool oldIsEmpty = sessions.empty();
  ShowContext(moved);
  if (oldIsEmpty) {
    // Last: it invalidates `context`. Another context shows already.
    DeleteContext(key, /*reshow=*/false);
  }
}

void SideView::OnSessionPagesCleared(wxCommandEvent &event) {
  event.Skip();
  Clear();
}

void SideView::OnSessionPageActivity(PageViewEvent &event) {
  event.Skip();
  // The program of a session did something: the files may have changed.
  if (Context *context = ShownContext()) {
    context->git->Activate();
  }
}

void SideView::OnFileSaved(FileEvent &event) {
  event.Skip();
  // Only a file of the account that shows can be in its repository.
  if (Context *context = ShownContext();
      context != nullptr &&
      event.GetAccount().GetId() == context->account.GetId()) {
    context->git->Activate();
  }
}

void SideView::OnTreeConnected(FileTreeConnectedEvent &event) {
  event.Skip(); // Other listeners of the notifier may own the request
  const int requestId = event.GetRequestId();
  const auto it = std::find_if(
      m_contexts.begin(), m_contexts.end(), [requestId](const auto &entry) {
        return entry.second.connectRequestId == requestId;
      });
  if (it == m_contexts.end()) {
    return; // Not ours, or the context is gone (and its tree with it)
  }
  Context &context = it->second;
  context.connectRequestId = 0;
  if (!context.tree->FinishConnect(event)) {
    KLOG_WARN() << "Could not connect to " << context.account.GetDisplayName()
                << ": " << event.GetStatus().message();
    context.connectStarted = false; // Tried again when it is shown again
  }
}

// A file is showing: its place in the tree that shows is selected, if the file
// is inside it (the path is converted between this machine and WSL when the
// tree is of the other one). Nothing happens when the file explorer is not on
// the screen, and a remote host is never listed.
void SideView::OnFileReveal(FileEvent &event) {
  event.Skip();
  const Account &account = event.GetAccount();
  if (!m_explorerBook->IsShownOnScreen() || account.IsRemote()) {
    return;
  }
  Context *context = ShownContext();
  if (context == nullptr) {
    return;
  }
  // The path as the tree writes it.
  const wxString &filePath = event.GetPath();
  wxString path;
  if (context->account.GetId() == account.GetId()) {
    path = filePath;
  } else if (context->account.IsLocalHost() && account.IsWSL()) {
    path = wsl::ToWindowsPath(account.m_name, filePath);
  } else if (context->account.IsWSL() && account.IsLocalHost()) {
    if (!wsl::ToLinuxPath(context->account.m_name, filePath, &path)) {
      return;
    }
  } else {
    return;
  }
  context->tree->SelectPath(path);
}
