#include "app/SessionGroup.h"
#include "core/Logger.h"

SessionGroup::SessionGroup(const wxString &groupName, bool terminalsGroup)
    : m_groupName{groupName}, m_terminalsGroup{terminalsGroup} {}

bool SessionGroup::AddSession(SessionPage *page) {
  if (page == nullptr) {
    KLOG_ERROR() << "Can not add null session page.";
    return false;
  }

  if (FindByName(page->GetSession().name) != wxNOT_FOUND) {
    KLOG_WARN() << "A session with this name already exist";
    return false;
  }

  page->GetSession().groupName = GetGroupName();
  m_sessions.push_back(page);
  return true;
}

SessionPage *SessionGroup::RemoveSession(const wxString &name) {
  int where = FindByName(name);
  if (where == wxNOT_FOUND) {
    return nullptr;
  }

  auto *page = m_sessions[where];
  m_sessions.erase(m_sessions.begin() + where);
  if (m_lastActive == page) {
    m_lastActive = nullptr;
  }
  return page;
}

int SessionGroup::FindByName(const wxString &name) const {
  for (size_t i = 0; i < m_sessions.size(); ++i) {
    if (m_sessions[i]->GetSession().name == name) {
      return static_cast<int>(i);
    }
  }
  return wxNOT_FOUND;
}

SessionPage *SessionGroup::GetSessionByName(const wxString &name) const {
  int where = FindByName(name);
  return where == wxNOT_FOUND ? nullptr : m_sessions[where];
}

void SessionGroup::Apply(std::function<void(SessionPage *)> func) {
  if (func == nullptr || IsEmpty()) {
    return;
  }
  for (auto *session : m_sessions) {
    func(session);
  }
}

void SessionGroup::SetGroupName(const wxString &groupName) {
  m_groupName = groupName;
  for (auto *session : m_sessions) {
    session->GetSession().groupName = groupName;
  }
}
