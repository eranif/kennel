#include "core/IEditor.h"

std::optional<LspServerKey> IEditor::GetServerKey() const {
  const std::optional<Account> account = GetSessionAccount();
  if (!account) {
    return std::nullopt;
  }
  // The server of the language of the file.
  switch (GetLanguageId()) {
  case LspLanguage::kNone:
    return std::nullopt;
  case LspLanguage::kC:
  case LspLanguage::kCpp:
  case LspLanguage::kObjectiveC:
  case LspLanguage::kObjectiveCpp:
    return LspServerKey{*account, LspKind::kClangd};
  case LspLanguage::kGo:
    return LspServerKey{*account, LspKind::kGopls};
  case LspLanguage::kRust:
    return LspServerKey{*account, LspKind::kRustAnalyzer};
  case LspLanguage::kPython:
    return LspServerKey{*account, LspKind::kPyright};
  case LspLanguage::kPhp:
    return LspServerKey{*account, LspKind::kPhpantom};
  }
  return std::nullopt;
}
