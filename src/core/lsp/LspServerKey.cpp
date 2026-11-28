#include "core/lsp/LspServerKey.h"

#include "core/WslPath.h"
#include "core/lsp/Clangd.h"
#include "core/lsp/Phpantom.h"
#include "core/lsp/RustAnalyzer.h"

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
  case LspKind::kPyright:
    return "pyright";
  case LspKind::kPhpantom:
    return "phpantom_lsp";
  }
  return "?";
}

const ToolDescription *LspServerKey::GetTool() const {
  switch (m_kind) {
  case LspKind::kClangd:
    return &clangd::Tool();
  case LspKind::kRustAnalyzer:
    return &rustanalyzer::Tool();
  case LspKind::kPhpantom:
    return &phpantom::Tool();
  case LspKind::kGopls:
  case LspKind::kPyright:
    break; // No release to download: Go and Node install them
  }
  return nullptr;
}

wxString LspServerKey::GetBinaryName() const {
  switch (m_kind) {
  case LspKind::kClangd:
    return "clangd";
  case LspKind::kGopls:
    return "gopls";
  case LspKind::kRustAnalyzer:
    return "rust-analyzer";
  case LspKind::kPyright:
    return "pyright-langserver";
  case LspKind::kPhpantom:
    return "phpantom_lsp";
  }
  return wxEmptyString;
}

wxString LspServerKey::GetInstallHint() const {
  switch (m_kind) {
  case LspKind::kGopls:
    return "go install golang.org/x/tools/gopls@latest";
  case LspKind::kPyright:
    return "npm install -g pyright";
  case LspKind::kClangd:
  case LspKind::kRustAnalyzer:
  case LspKind::kPhpantom:
    break; // Kennel installs these
  }
  return wxEmptyString;
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
