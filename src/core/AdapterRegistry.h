#pragma once

#include <wx/string.h>

#include "core/Config.h"

#include <vector>

// Indexes agents from AppConfig for fast lookup by name. Nothing throws.
class AdapterRegistry {
public:
  explicit AdapterRegistry(const AppConfig &config);

  void Rebuild(const AppConfig &config);

  // Returns the agent with the given name, or nullptr if none matches.
  const AgentDef *FindAgent(const wxString &name) const;

  // All agent names, in config order.
  std::vector<wxString> AgentNames() const;

  size_t AgentCount() const { return m_agents.size(); }

  const std::vector<AgentDef> &Agents() const { return m_agents; }

private:
  std::vector<AgentDef> m_agents;
};
