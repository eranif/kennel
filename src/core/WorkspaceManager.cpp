#include "core/WorkspaceManager.h"

#include "core/Logger.h"

WorkspaceManager::WorkspaceManager(AppPaths paths, WorkspaceStore *store,
                                   const AdapterRegistry *registry)
    : m_paths(std::move(paths)), m_store(store), m_registry(registry) {}

Status WorkspaceManager::Load() {
  StatusOr<Workspace> ws = m_store->Load();
  if (!ws.ok()) {
    return ws.status();
  }
  m_version = ws.value().version;
  m_sessions = ws.value().sessions;
  return Status::Ok();
}

const Session *WorkspaceManager::Find(const wxString &name) const {
  for (const Session &s : m_sessions) {
    if (s.name == name) {
      return &s;
    }
  }
  return nullptr;
}

std::vector<Session>::iterator WorkspaceManager::FindIt(const wxString &name) {
  for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
    if (it->name == name) {
      return it;
    }
  }
  return m_sessions.end();
}

StatusOr<Session> WorkspaceManager::Create(const NewSessionRequest &req) {
  if (req.name.empty()) {
    return Status::Error("Session name must not be empty");
  }
  if (Find(req.name) != nullptr) {
    return Status::Error(
        wxString::Format("A session named '%s' already exists", req.name));
  }

  Session s;
  s.name = req.name;
  s.agentName = req.agentName;
  s.workingDir = req.workingDir;
  s.groupName = req.groupName;
  s.plainTerminal = req.plainTerminal;
  m_sessions.push_back(s);
  KLOG_INFO() << "Created session '" << s.name << "' (agent " << s.agentName
              << ")";
  return s;
}

Status WorkspaceManager::Rename(const wxString &from, const wxString &to) {
  auto it = FindIt(from);
  if (it == m_sessions.end()) {
    return Status::Error(wxString::Format("No session named '%s'", from));
  }
  if (from == to) {
    return Status::Ok();
  }
  if (to.empty()) {
    return Status::Error("Session name must not be empty");
  }
  if (Find(to) != nullptr) {
    return Status::Error(
        wxString::Format("A session named '%s' already exists", to));
  }

  KLOG_INFO() << "Renamed session '" << from << "' to '" << to << "'";
  it->name = to;
  return Status::Ok();
}

Status WorkspaceManager::RenameGroup(const wxString &from, const wxString &to) {
  if (from == to) {
    return Status::Ok();
  }
  if (to.empty()) {
    return Status::Error("Group name must not be empty");
  }
  bool found = false;
  for (const Session &s : m_sessions) {
    if (s.groupName == to) {
      return Status::Error(
          wxString::Format("A group named '%s' already exists", to));
    }
    if (s.groupName == from) {
      found = true;
    }
  }
  if (!found) {
    return Status::Error(wxString::Format("No group named '%s'", from));
  }

  for (Session &s : m_sessions) {
    if (s.groupName == from) {
      s.groupName = to;
    }
  }
  KLOG_INFO() << "Renamed group '" << from << "' to '" << to << "'";
  return Status::Ok();
}

Status WorkspaceManager::MoveSession(const wxString &name,
                                     const wxString &toGroup) {
  if (toGroup.empty()) {
    return Status::Error("Group name must not be empty");
  }
  auto it = FindIt(name);
  if (it == m_sessions.end()) {
    return Status::Error(wxString::Format("No session named '%s'", name));
  }
  KLOG_INFO() << "Moved session '" << name << "' from group '" << it->groupName
              << "' to '" << toGroup << "'";
  it->groupName = toGroup;
  return Status::Ok();
}

Status WorkspaceManager::Close(const wxString &name) {
  auto it = FindIt(name);
  if (it == m_sessions.end()) {
    return Status::Error(wxString::Format("No session named '%s'", name));
  }
  m_sessions.erase(it);
  KLOG_INFO() << "Closed session '" << name << "'";
  return Status::Ok();
}

Status WorkspaceManager::Persist() {
  Workspace ws;
  ws.version = m_version;
  ws.sessions = m_sessions;
  Status st = m_store->Save(ws);
  if (!st.ok()) {
    KLOG_ERROR() << "Failed to persist workspace.json: " << st.message();
  }
  return st;
}

void WorkspaceManager::CloseAll() { m_sessions.clear(); }
