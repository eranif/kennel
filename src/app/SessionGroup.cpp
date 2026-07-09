#include "app/SessionGroup.h"
#include "app/MainFrame.h"
#include "core/AppManager.h"
#include "core/Logger.h"
#include "terminal_view.h"

#include <wx/sizer.h>

SessionGroup::SessionGroup(wxWindow *parent, const wxString &groupName,
                           bool terminalsGroup)
    : wxPanel(parent), m_groupName{groupName},
      m_terminalsGroup{terminalsGroup} {
  SetSizer(new wxBoxSizer(wxVERTICAL));
  m_book = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxAUI_NB_TAB_MOVE | wxAUI_NB_TAB_FIXED_WIDTH |
                                 wxAUI_NB_TAB_SPLIT);
  GetSizer()->Add(m_book, wxSizerFlags(1).Expand());

  Bind(wxEVT_SESSION_IDLE, &SessionGroup::OnSessionIdle, this);
  Bind(wxEVT_SESSION_ACTIVE, &SessionGroup::OnSessionActive, this);
  Bind(wxEVT_SESSION_EXITED, &SessionGroup::OnSessionExited, this);
  Bind(wxEVT_IDLE, &SessionGroup::OnIdleEvent, this);
}

SessionGroup::~SessionGroup() {
  Unbind(wxEVT_SESSION_IDLE, &SessionGroup::OnSessionIdle, this);
  Unbind(wxEVT_SESSION_ACTIVE, &SessionGroup::OnSessionActive, this);
  Unbind(wxEVT_SESSION_EXITED, &SessionGroup::OnSessionExited, this);
  Unbind(wxEVT_IDLE, &SessionGroup::OnIdleEvent, this);
}

Status SessionGroup::AddSessionPage(SessionPage *page) {
  if (page == nullptr)
    return Status::Error("Invalid nullptr page");

  if (FindByName(page->GetSession().name) != wxNOT_FOUND)
    return Status::Error("A session with this name already exist");

  const auto &session = page->GetSession();
  page->Reparent(m_book);
  m_book->AddPage(page, session.name, true);
  m_history.Push(Tab{
      .title = session.name,
      .agentName = session.agentName,
  });
  return Status::Ok();
}

StatusOr<SessionPage *> SessionGroup::NewSessionPage(const Session &session,
                                                     bool resume) {
  auto &registry = AppManager::Get().Adapters();
  const AgentDef *agent = registry.FindAgent(session.agentName);
  if (agent == nullptr) {
    return Status::Error(
        wxString::Format("No such agent: %s", session.agentName));
  }

  auto *page = new SessionPage(m_book, *agent, session, resume);
  auto result = AddSessionPage(page);
  if (!result.ok()) {
    wxDELETE(page);
    return result;
  }
  return page;
}

StatusOr<SessionPage *> SessionGroup::RemoveSessionPage(const wxString &name) {
  int where = FindByName(name);
  if (where == wxNOT_FOUND) {
    return Status::Error("No such session");
  }

  auto *session = GetSessionByIndex(where);
  m_book->RemovePage(where);
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
      result.value()->GetTerminal()->EnsureStarted();
      ++restored;
    }
  }
  KLOG_INFO() << "Restored " << restored << " session(s)";
}

void SessionGroup::OnSessionExited(wxCommandEvent &e) {
  wxString name = e.GetString();
  DeleteByName(name);
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

bool SessionGroup::DeleteByName(const wxString &name) {
  for (size_t i = 0; i < m_book->GetPageCount(); ++i) {
    if (m_book->GetPageText(i) == name) {
      m_book->DeletePage(i);
      return true;
    }
  }
  return false;
}

void SessionGroup::OnIdleEvent(wxIdleEvent &e) {
  if (!m_idleHandled && GetActiveTerminal()) {
    m_idleHandled = true;
    GetActiveTerminal()->SetFocus();
  }
}

SessionPage *SessionGroup::GetActiveTerminal() {
  if (IsEmpty())
    return nullptr;
  return dynamic_cast<SessionPage *>(m_book->GetPage(m_book->GetSelection()));
}

void SessionGroup::SetGroupName(const wxString &groupName) {
  m_groupName = groupName;
  for (size_t i = 0; i < m_book->GetPageCount(); ++i) {
    auto *session = static_cast<SessionPage *>(m_book->GetPage(i));
    session->GetSession().groupName = groupName;
  }
}

int SessionGroup::FindByName(const wxString &name) const {
  for (size_t i = 0; i < m_book->GetPageCount(); ++i) {
    if (m_book->GetPageText(i) == name) {
      m_book->DeletePage(i);
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
  if (IsTerminalsGroup() || GetActiveTerminal() == nullptr)
    return;

  GetActiveTerminal()->CallAfter(&SessionPage::Restart);
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
