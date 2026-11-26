#include "core/lsp/LspServerKey.h"

#include "core/WslPath.h"

#include <wx/filename.h>

TransportType LspServerKey::GetTransportType() const {
  switch (m_account.m_type) {
  case HostType::kWSL:
    return TransportType::kWsl;
  case HostType::kRemote:
    return TransportType::kSsh;
  case HostType::kLocalHost:
    break;
  }
  return TransportType::kLocal;
}

wxString LspServerKey::GetKindName() const {
  switch (m_kind) {
  case LspKind::kClangd:
    return "clangd";
  case LspKind::kGopls:
    return "gopls";
  case LspKind::kRustAnalyzer:
    return "rust-analyzer";
  }
  return "?";
}

wxString LspServerKey::GetProgram(const wxString &installedPath) const {
  static const wxString kHome = "$HOME";
  if (!installedPath.StartsWith(kHome)) {
    return installedPath;
  }
  const wxString rest = installedPath.Mid(kHome.size());
  switch (m_account.m_type) {
  case HostType::kWSL:
    return wsl::HomeDir(m_account.m_name) + rest;
  case HostType::kRemote:
    return installedPath;
  case HostType::kLocalHost:
    break;
  }
  wxString path = wxFileName::GetHomeDir() + rest;
#ifdef __WXMSW__
  path.Replace("/", "\\");
#endif
  return path;
}
