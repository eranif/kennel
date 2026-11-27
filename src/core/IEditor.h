#pragma once

#include "core/Account.h"
#include "core/FilePageUID.h"
#include "core/lsp/LspEvent.h"
#include "core/lsp/LspLanguage.h"
#include "core/lsp/LspServerKey.h"

#include <cstdint>
#include <optional>
#include <wx/string.h>

// What the core classes (the LspManager) know of an editor: a text of a file of
// an account. The page that shows the file implements it, and the core never
// sees the page or its controls. GUI-free.
class IEditor {
public:
  virtual ~IEditor() = default;

  // Whose file it is: this machine, a WSL distribution or a remote host.
  virtual Account GetAccount() const = 0;
  // The path of the file in the notation of GetAccount() (a Linux path for a
  // WSL file).
  virtual wxString GetFilePath() const = 0;
  // The language of the file, or LspLanguage::kNone when the editor has no file
  // that a language server can have (a diff, an untitled page, another
  // language).
  virtual LspLanguage GetLanguageId() const = 0;
  // The text of the editor as it is now.
  virtual wxString GetText() const = 0;
  // Tells this editor from every other one, for as long as it lives: it is
  // given when the editor is made, goes up with each editor and is never used
  // twice.
  virtual FilePageUID GetEditorUID() const = 0;
  // Counts the changes of the text (an insert or a delete). It goes up at each
  // change and never goes back, not even when the file is loaded again: whoever
  // keeps a number can tell by it that the text is not the one it knows.
  virtual uint64_t GetChangeCounter() const = 0;

  // The machine and the root directory that the file belongs to: the account
  // of the session that holds the file, found once when the file is loaded (the
  // deepest root when several sessions hold it) and never changed after. The
  // m_rootDir of the account is that root directory. Nothing when no session
  // held the file.
  virtual std::optional<Account> GetSessionAccount() const = 0;

  // The language server of the file, made from GetSessionAccount() and
  // GetLanguageId(). Nothing for a file that has no language server, or that
  // no session held.
  std::optional<LspServerKey> GetServerKey() const;
};
