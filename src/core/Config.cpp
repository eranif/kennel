#include "core/Config.h"
#include <wx/font.h>

AppConfig DefaultConfig() {
  AppConfig cfg;
  cfg.version = 1;
  return cfg;
}

#ifdef __WXMAC__
constexpr int kDefaultFontSize = 20;
#else
constexpr int kDefaultFontSize = 14;
#endif

wxString GetDefaultFontFamily() {
#ifdef __WXMAC__
  return "Menlo";
#elif defined(__WXMSW__)
  return "Consolas";
#else
  return wxFont(wxFontInfo(kDefaultFontSize).Family(wxFONTFAMILY_TELETYPE))
      .GetFaceName();
#endif
}

int GetDefaultFontSize() { return kDefaultFontSize; }

wxArrayString AppConfig::GetAgentNames() const {
  wxArrayString names;
  for (const auto &a : agents) {
    names.push_back(a.name);
  }
  return names;
}

void AppConfig::Merge(const AppConfig &other) {
  std::map<wxString, AgentDef> agentMap;
  for (const auto &a : agents) {
    agentMap.insert({a.name, a});
  }
  for (const auto &a : other.agents) {
    agentMap.insert({a.name, a});
  }
  agents.clear();
  for (const auto &[_, a] : agentMap) {
    agents.push_back(a);
  }
}
