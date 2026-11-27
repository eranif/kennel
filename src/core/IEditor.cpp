#include "core/IEditor.h"

std::optional<LspServerKey> IEditor::GetServerKey() const {
  // Every language of LspLanguage is served by clangd for now.
  if (GetLanguageId() == LspLanguage::kNone) {
    return std::nullopt;
  }
  const std::optional<Account> account = GetSessionAccount();
  if (!account) {
    return std::nullopt;
  }
  return LspServerKey{*account, LspKind::kClangd};
}
