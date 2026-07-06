#include "core/AdapterRegistry.h"

AdapterRegistry::AdapterRegistry(const AppConfig &config) { Rebuild(config); }

void AdapterRegistry::Rebuild(const AppConfig &config) {
  m_agents = config.agents;
}

const AgentDef *AdapterRegistry::FindAgent(const wxString &name) const {
  for (const AgentDef &a : m_agents) {
    if (a.name == name) {
      return &a;
    }
  }
  return nullptr;
}

std::vector<wxString> AdapterRegistry::AgentNames() const {
  std::vector<wxString> names;
  names.reserve(m_agents.size());
  for (const AgentDef &a : m_agents) {
    names.push_back(a.name);
  }
  return names;
}
