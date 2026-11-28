#include "core/lsp/LspSemanticTokens.h"

#include "core/json.hpp"

#include <set>

using json = nlohmann::json;

wxDEFINE_EVENT(wxEVT_LSP_SEMANTIC_TOKENS, LspSemanticTokensEvent);

std::optional<LspSemanticType>
LspSemanticTokens::FindType(const std::string &tokenType) const {
  if (tokenType == "variable" || tokenType == "parameter" ||
      tokenType == "property" || tokenType == "enumMember") {
    return LspSemanticType::kVariable;
  }
  if (tokenType == "class" || tokenType == "struct" ||
      tokenType == "interface" || tokenType == "enum" || tokenType == "type" ||
      tokenType == "typeParameter") {
    return LspSemanticType::kClass;
  }
  if (tokenType == "function" || tokenType == "method") {
    return LspSemanticType::kFunction;
  }
  return std::nullopt;
}

StatusOr<LspSemanticWords>
LspSemanticTokens::GetWords(const std::string &result,
                            const wxString &text) const {
  const json capabilities =
      json::parse(m_serverCapabilities, nullptr, /*allow_exceptions=*/false);
  if (!capabilities.is_object()) {
    return Status::Error("The capabilities of the server are not JSON.");
  }
  // The offsets are taken as UTF-8 bytes, whatever the server says it counts
  // in (a server that does not offer "utf-8" counts in UTF-16: the offsets are
  // the same on a line that is ASCII, which is most of them).
  if (!capabilities.contains("semanticTokensProvider") ||
      !capabilities["semanticTokensProvider"].is_object() ||
      !capabilities["semanticTokensProvider"].contains("legend")) {
    return Status::Error("The server has no semantic tokens.");
  }
  const json &legend = capabilities["semanticTokensProvider"]["legend"];
  if (!legend.is_object() || !legend.contains("tokenTypes") ||
      !legend["tokenTypes"].is_array()) {
    return Status::Error("The server has no legend of semantic tokens.");
  }
  // The kind of word of each token type of the legend (nothing: not coloured).
  std::vector<std::optional<LspSemanticType>> types;
  for (const json &name : legend["tokenTypes"]) {
    types.push_back(name.is_string() ? FindType(name.get<std::string>())
                                     : std::nullopt);
  }

  const json answer = json::parse(result, nullptr, /*allow_exceptions=*/false);
  LspSemanticWords words;
  if (answer.is_null() || answer.is_discarded()) {
    return words; // The server has nothing for the text
  }
  if (!answer.is_object() || !answer.contains("data") ||
      !answer["data"].is_array()) {
    return Status::Error("The answer has no semantic tokens.");
  }
  const json &data = answer["data"];

  // Where each line of the text starts, in bytes.
  const std::string bytes = text.ToStdString(wxConvUTF8);
  std::vector<size_t> lineStarts{0};
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (bytes[i] == '\n') {
      lineStarts.push_back(i + 1);
    }
  }

  // Five numbers for a token: the line and the start (both relative to the
  // token before), the length, the type and the modifiers.
  std::map<LspSemanticType, std::set<std::string>> found;
  size_t line = 0;
  size_t start = 0;
  for (size_t i = 0; i + 4 < data.size(); i += 5) {
    if (!data[i].is_number_unsigned() || !data[i + 1].is_number_unsigned() ||
        !data[i + 2].is_number_unsigned() ||
        !data[i + 3].is_number_unsigned()) {
      return Status::Error("The semantic tokens are not numbers.");
    }
    const size_t deltaLine = data[i].get<size_t>();
    const size_t deltaStart = data[i + 1].get<size_t>();
    const size_t length = data[i + 2].get<size_t>();
    const size_t typeIndex = data[i + 3].get<size_t>();
    line += deltaLine;
    start = deltaLine == 0 ? start + deltaStart : deltaStart;
    if (typeIndex >= types.size() || !types[typeIndex] ||
        line >= lineStarts.size()) {
      continue;
    }
    const size_t from = lineStarts[line] + start;
    if (length == 0 || from + length > bytes.size()) {
      continue;
    }
    const std::string word = bytes.substr(from, length);
    if (word.find('\n') != std::string::npos) {
      continue; // A token of several lines is not a word
    }
    // Offsets in UTF-16 on a line with other than ASCII can cut a character.
    if (wxString::FromUTF8(word).empty()) {
      continue;
    }
    found[*types[typeIndex]].insert(word);
  }

  for (const auto &[type, set] : found) {
    for (const std::string &word : set) {
      words[type].push_back(wxString::FromUTF8(word));
    }
  }
  return words;
}
