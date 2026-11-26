#pragma once

#include "core/Account.h"
#include "core/ToolDescription.h"
#include "core/lsp/Clangd.h"
#include "core/lsp/LspServer.h"

#include <wx/string.h>

// Which language server: a server is told from another by the machine, the
// working directory and the language (a server speaks one language). There is
// at most one for each key. GUI-free.
struct LspServerKey {
  // The machine. Its m_rootDir is the working directory of the server (the
  // one of a session).
  Account m_account;
  LspKind m_kind{LspKind::kClangd};

  // The program that Kennel can install for the server, or null when there is
  // none for this kind.
  const ToolDescription *GetTool() const {
    return m_kind == LspKind::kClangd ? &clangd::Tool() : nullptr;
  }

  // How the server of this machine is run (see LspServer).
  TransportType GetTransportType() const;

  // The program of the server as LspServer runs it, from the path that
  // RemoteInstaller reports ("$HOME/.kennel/bin/clangd"): the path of this
  // machine, the Linux path for WSL, and the path with its "$HOME" for a remote
  // host (the shell of the host expands it).
  wxString GetProgram(const wxString &installedPath) const;

  // The name of the kind of the server: "clangd", "gopls", "rust-analyzer".
  wxString GetKindName() const;

  // The key as one text, fit to be the key of a map and to be read in a log:
  // "clangd(machine=LocalHost, rootDir=C:/src/app)". The machine is
  // "LocalHost", or Account::GetId() for the others.
  wxString GetKey() const {
    const wxString machine =
        m_account.IsLocalHost() ? wxString("LocalHost") : m_account.GetId();
    return GetKindName() + "(machine=" + machine +
           ", rootDir=" + m_account.m_rootDir + ")";
  }
};
