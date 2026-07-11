#include "app/SessionGroup.h"
#include "app/AssetBootstrap.h"
#include "app/AuiTabArt.h"
#include "app/MainFrame.h"
#include "core/AppManager.h"
#include "core/Logger.h"
#include "terminal_view.h"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>

wxDEFINE_EVENT(wxEVT_GROUP_PAGE_CHANGED, SessionGroupEvent);
wxDEFINE_EVENT(wxEVT_GROUP_LAST_PAGE_CLOSED, SessionGroupEvent);
wxDEFINE_EVENT(wxEVT_GROUP_MOVE_TO_GROUP, SessionGroupEvent);

SessionGroup::SessionGroup(wxWindow *parent, const wxString &groupName,
                           bool terminalsGroup)
    : wxPanel(parent), m_groupName{groupName},
      m_terminalsGroup{terminalsGroup} {
  SetSizer(new wxBoxSizer(wxVERTICAL));
  m_book = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxAUI_NB_TAB_MOVE | wxAUI_NB_TAB_FIXED_WIDTH |
                                 wxAUI_NB_TAB_SPLIT);
  m_book->SetArtProvider(new AuiFlatTabArt());
  if (ThemeManager::Get().ActiveTheme()) {
    m_book->SetBackgroundColour(ThemeManager::Get().ActiveTheme()->bg);
  }

  GetSizer()->Add(m_book, wxSizerFlags(1).Expand());

  Bind(wxEVT_SESSION_IDLE, &SessionGroup::OnSessionIdle, this);
  Bind(wxEVT_SESSION_ACTIVE, &SessionGroup::OnSessionActive, this);
  Bind(wxEVT_SESSION_EXITED, &SessionGroup::OnSessionExited, this);
  Bind(wxEVT_IDLE, &SessionGroup::OnIdleEvent, this);
  m_book->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &SessionGroup::OnPageChanged,
               this);
  m_book->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSED, &SessionGroup::OnPageClosed,
               this);
  m_book->Bind(wxEVT_AUINOTEBOOK_TAB_RIGHT_DOWN, &SessionGroup::OnContextMenu,
               this);
}

SessionGroup::~SessionGroup() {
  Unbind(wxEVT_SESSION_IDLE, &SessionGroup::OnSessionIdle, this);
  Unbind(wxEVT_SESSION_ACTIVE, &SessionGroup::OnSessionActive, this);
  Unbind(wxEVT_SESSION_EXITED, &SessionGroup::OnSessionExited, this);
  Unbind(wxEVT_IDLE, &SessionGroup::OnIdleEvent, this);
}

bool SessionGroup::AddSessionPage(SessionPage *page) {
  if (page == nullptr) {
    KLOG_ERROR() << "Can not add null session page.";
    return false;
  }

  if (FindByName(page->GetSession().name) != wxNOT_FOUND) {
    KLOG_WARN() << "A session with this name already exist";
    return false;
  }
  const auto &session = page->GetSession();

  wxBitmapBundle bmp{};
  if (!session.plainTerminal) {
    const auto *agentDef =
        AppManager::Get().Adapters().FindAgent(session.agentName);
    if (agentDef) {
      const wxString path = ResolveIconPath(agentDef->iconPath);
      if (!path.empty() && wxFileExists(path)) {
        bmp = wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
      }
    }
  }

  page->Reparent(m_book);
  m_book->AddPage(page, session.name, true, bmp);
  return true;
}

SessionPage *SessionGroup::NewSessionPage(const Session &session, bool resume) {
  std::optional<AgentDef> agent{std::nullopt};
  if (IsSessionGroup()) {
    auto &registry = AppManager::Get().Adapters();
    const AgentDef *pagent = registry.FindAgent(session.agentName);
    if (pagent == nullptr) {
      KLOG_ERROR() << wxString::Format("No such agent: %s", session.agentName);
      return nullptr;
    }
    agent = *pagent;
  }

  auto *page = new SessionPage(m_book, agent, session, resume);
  if (page->Status() == SessionStatus::Starting) {
    // Could not start the session
    wxDELETE(page);
    return nullptr;
  }

  if (!AddSessionPage(page)) {
    wxDELETE(page);
    return nullptr;
  }
  return page;
}

std::vector<SessionPage *> SessionGroup::GetAllSessions() const {
  std::vector<SessionPage *> result;
  for (size_t i = 0; i < m_book->GetPageCount(); ++i) {
    auto *page = GetSessionByIndex(i);
    if (page != nullptr) {
      result.push_back(page);
    }
  }
  return result;
}

StatusOr<SessionPage *> SessionGroup::RemoveSessionPage(const wxString &name) {
  int where = FindByName(name);
  if (where == wxNOT_FOUND) {
    return Status::Error("No such session");
  }

  auto *session = GetSessionByIndex(where);
  m_book->RemovePage(where);
  NotifyLastPageClosedIfEmpty();
  return session;
}

void SessionGroup::RestoreSessions() {
  auto &workspace = AppManager::Get().Workspace();
  const auto &sessions = workspace.Sessions();
  if (sessions.empty()) {
    return;
  }

  const auto &prefs = AppManager::Get().GetPrefs();
  int restored = 0;

  for (const Session &s : sessions) {
    auto result = NewSessionPage(s, true);
    if (result) {
      result->GetTerminal()->EnsureStarted();
      ++restored;
    }
  }
  KLOG_INFO() << "Restored " << restored << " session(s)";
}

void SessionGroup::OnSessionExited(wxCommandEvent &e) {
  wxString name = e.GetString();
  DeleteSessionByName(name);
}

void SessionGroup::OnSessionIdle(wxCommandEvent &e) {
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

void SessionGroup::OnSessionActive(wxCommandEvent &e) { e.Skip(); }

bool SessionGroup::DeleteSessionByName(const wxString &name) {
  for (size_t i = 0; i < m_book->GetPageCount(); ++i) {
    if (m_book->GetPageText(i) == name) {
      CloseSession(name, i);
      return true;
    }
  }
  return false;
}

void SessionGroup::OnIdleEvent(wxIdleEvent &e) {
  if (!m_idleHandled && GetActivePage()) {
    m_idleHandled = true;
    GetActivePage()->SetFocus();
  }
}

SessionPage *SessionGroup::GetActivePage() {
  if (IsEmpty())
    return nullptr;
  return dynamic_cast<SessionPage *>(m_book->GetPage(m_book->GetSelection()));
}

wxString SessionGroup::Rename() {
  wxString newName = ::wxGetTextFromUser(_("Choose new group name:"), "Kennel",
                                         GetGroupName(), this);
  if (newName.empty() || newName == GetGroupName()) {
    return {};
  }
  SetGroupName(newName);
  return newName;
}

void SessionGroup::SetGroupName(const wxString &groupName) {
  wxString oldName = GetGroupName();
  m_groupName = groupName;
  for (size_t i = 0; i < m_book->GetPageCount(); ++i) {
    auto *session = static_cast<SessionPage *>(m_book->GetPage(i));
    session->GetSession().groupName = groupName;
  }

  auto &workspace = AppManager::Get().Workspace();
  if (Status st = workspace.RenameGroup(oldName, groupName); !st.ok()) {
    wxMessageBox(st.message(), "Kennel", wxOK | wxICON_ERROR, this);
    return;
  }
  workspace.Persist();
}

int SessionGroup::FindByName(const wxString &name) const {
  for (size_t i = 0; i < m_book->GetPageCount(); ++i) {
    if (m_book->GetPageText(i) == name) {
      return i;
    }
  }
  return wxNOT_FOUND;
}

SessionPage *SessionGroup::GetSessionByIndex(int index) const {
  if (index < 0 || index >= static_cast<int>(m_book->GetPageCount()))
    return nullptr;
  return static_cast<SessionPage *>(m_book->GetPage(index));
}

void SessionGroup::Apply(std::function<void(SessionPage *)> func) {
  if (func == nullptr || IsEmpty()) {
    return;
  }

  for (size_t i = 0; i < m_book->GetPageCount(); ++i) {
    auto *session = GetSessionByIndex(i);
    if (session)
      func(session);
  }
}

void SessionGroup::SelectSession(const wxString &sessionName) {
  int where = FindByName(sessionName);
  if (where == wxNOT_FOUND)
    return;
  m_book->SetSelection(where);
}

void SessionGroup::SelectSession(bool forward) {
  if (IsEmpty() || m_book->GetPageCount() == 1)
    return;

  int where = m_book->GetSelection();
  if (where == wxNOT_FOUND)
    return;

  if (forward) {
    if (static_cast<size_t>((where + 1)) >= m_book->GetPageCount()) {
      where = 0;
    } else {
      where += 1;
    }
  } else {
    where -= 1;
    if (where < 0) {
      where = m_book->GetPageCount() - 1;
    }
  }
  m_book->SetSelection(where);
}

void SessionGroup::ApplyTheme(const wxString &themeName) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetTheme(themeName);
  if (!active) {
    return;
  }

  auto callback = [active](SessionPage *sp) {
    sp->ApplyTheme(*active);
    sp->GetTerminal()->SendSizeEvent();
  };

  Apply(callback);
  if (themeMgr.ActiveTheme()) {
    m_book->SetBackgroundColour(themeMgr.ActiveTheme()->bg);
    m_book->Refresh();
  }
  SendSizeEvent(); // Force the terminals to recalculate their size
}

void SessionGroup::ApplyOptimizedDrawing() {
  bool optimized = AppManager::Get().GetPrefs().terminalOptimizedDrawing;
  auto callback = [optimized](SessionPage *sp) {
    sp->GetTerminal()->EnableSafeDrawing(!optimized);
    sp->GetTerminal()->Refresh();
  };
  Apply(callback);
}

void SessionGroup::ApplyFont(const wxFont &f) {
  auto &themeMgr = ThemeManager::Get();
  auto active = themeMgr.SetFont(f);
  if (!active) {
    return;
  }

  auto callback = [active](SessionPage *sp) {
    sp->ApplyTheme(*active);
    sp->GetTerminal()->SendSizeEvent();
  };

  Apply(callback);
  SendSizeEvent(); // Force the terminals to recalculate their size
}

void SessionGroup::RefreshSelection() {
  if (IsTerminalsGroup() || GetActivePage() == nullptr)
    return;

  GetActivePage()->CallAfter(&SessionPage::Restart);
  m_pendingIdle++;
  if (m_pendingIdle) {
    GetMainFrame()->SetActivityText(
        wxString::Format(_("Refreshing %d sessions"), m_pendingIdle));
    GetMainFrame()->StartActivityIndicator();
  }
}

void SessionGroup::RefreshAll() {
  if (IsTerminalsGroup())
    return;

  auto RefreshSession = [this](SessionPage *page) {
    page->CallAfter(&SessionPage::Restart);
    m_pendingIdle++;
  };

  Apply(RefreshSession);
  if (m_pendingIdle) {
    GetMainFrame()->SetActivityText(
        wxString::Format(_("Refreshing %d sessions"), m_pendingIdle));
    GetMainFrame()->StartActivityIndicator();
  }
}

void SessionGroup::CloseActiveSession() {
  auto *page = dynamic_cast<SessionPage *>(m_book->GetCurrentPage());
  CHECK_NOT_NULL_RETURN(page);
  CloseSession(page->GetSession().name, m_book->GetSelection());
}

void SessionGroup::CloseAll() {
  m_book->DeleteAllPages();
  m_pendingIdle = 0;
  NotifyLastPageClosedIfEmpty();
}

void SessionGroup::OnPageChanged(wxAuiNotebookEvent &event) {
  event.Skip();
  SessionGroupEvent e{wxEVT_GROUP_PAGE_CHANGED};
  e.SetGroupName(GetGroupName());
  GetMainFrame()->GetMainView()->GetEventHandler()->AddPendingEvent(e);
}

void SessionGroup::OnPageClosed(wxAuiNotebookEvent &event) {
  event.Skip();
  NotifyLastPageClosedIfEmpty();
}

void SessionGroup::NotifyLastPageClosedIfEmpty() {
  if (m_book->GetPageCount() == 0) {
    SessionGroupEvent e{wxEVT_GROUP_LAST_PAGE_CLOSED};
    e.SetGroupName(GetGroupName());
    GetMainFrame()->GetMainView()->GetEventHandler()->AddPendingEvent(e);
  }
}

void SessionGroup::OnContextMenu(wxAuiNotebookEvent &event) {
  event.Skip();
  int index =
      m_book->HitTest(m_book->ScreenToClient(::wxGetMousePosition()), nullptr);
  if (index == wxNOT_FOUND) {
    return;
  }

  auto *page = dynamic_cast<SessionPage *>(m_book->GetPage(index));
  CHECK_NOT_NULL_RETURN(page);

  wxString sessionName = page->GetSession().name;
  KLOG_DEBUG() << "Will close session=" << sessionName << ", index=" << index;
  wxMenu menu;
  menu.Append(XRCID("session-group-close-session"), _("Close"),
              _("Close the active session"));
  menu.Bind(
      wxEVT_MENU,
      [index, sessionName, this](wxCommandEvent &) {
        CloseSession(sessionName, index);
      },
      XRCID("session-group-close-session"));
  m_book->PopupMenu(&menu);
}

void SessionGroup::CloseSession(const wxString &sessionName, int index) {
  if (m_book->DeletePage(index)) {
    NotifyLastPageClosedIfEmpty();
    AppManager::Get().Workspace().CloseSession(sessionName);
    AppManager::Get().Workspace().Persist();
  }
}