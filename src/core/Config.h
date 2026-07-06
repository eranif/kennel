#pragma once

#include <wx/arrstr.h>
#include <wx/filename.h>
#include <wx/string.h>

#include <map>
#include <vector>

wxString GetDefaultFontFamily();
int GetDefaultFontSize();

template <typename Container>
inline wxString JoinStrings(const Container &c, const wxString &glue = " ") {
  if (c.empty()) {
    return wxEmptyString;
  }
  wxString result;
  for (const auto &e : c) {
    result << e << glue;
  }
  result.RemoveLast(glue.length());
  return result;
}

struct GlobalSettings {};

// ---------------------------------------------------------------------------
// Agent definition (config.json -> "agents[]").
// ---------------------------------------------------------------------------
struct AgentDef {
  wxString name;
  wxString executable;
  std::vector<wxString> baseArgs;
  wxString resumeArg;
  wxString iconPath;
  std::vector<wxString> extraArgs;
  std::map<wxString, wxString> env;
  wxString remoteHost;
  wxString remoteUser;
  wxString loginShell;
  inline bool IsRemote() const { return !remoteHost.empty(); }
  inline bool IsWSL() const { return loginShell.Contains(R"(wsl.exe)"); }
  inline bool IsBash() const { return loginShell.Contains("bash"); }
};

// ---------------------------------------------------------------------------
// Whole config.json document.
// ---------------------------------------------------------------------------
struct AppConfig {
  int version = 1;
  GlobalSettings global;
  std::vector<AgentDef> agents;

  wxArrayString GetAgentNames() const;

  void Merge(const AppConfig &other);
};

AppConfig DefaultConfig();
