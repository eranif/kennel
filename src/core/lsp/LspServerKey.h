#pragma once

#include "core/Account.h"
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

  // The key as one text, fit to be the key of a map.
  wxString GetKey() const {
    return m_account.GetId() + "|" + m_account.m_rootDir + "|" +
           wxString::Format("%d", static_cast<int>(m_kind));
  }
};
