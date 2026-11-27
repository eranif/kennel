#pragma once

#include "core/Status.h"
#include "core/lsp/LspEvent.h"

#include <map>
#include <optional>
#include <string>
#include <vector>
#include <wx/string.h>

// The kinds of words that the editor colours (the server has many more).
enum class LspSemanticType { kVariable, kClass, kFunction };

// The words of a text, by their kind: { kClass => {"Foo", "Bar"}, kFunction =>
// {"foo", "baz"} }. Each word is there once.
using LspSemanticWords = std::map<LspSemanticType, std::vector<wxString>>;

// The answer of LspClient::GetSemanticHighlightsFor(): the words of the editor
// by their kind. GetRequestId() is the ID that the call gave. Sent at once
// (ProcessEvent).
class LspSemanticTokensEvent : public LspEvent {
public:
  explicit LspSemanticTokensEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : LspEvent(type, id) {}
  LspSemanticTokensEvent(const LspSemanticTokensEvent &) = default;
  wxEvent *Clone() const override { return new LspSemanticTokensEvent(*this); }

  const LspSemanticWords &GetWords() const { return m_words; }
  void SetWords(const LspSemanticWords &words) { m_words = words; }

private:
  LspSemanticWords m_words;
};
// The words that the server named in the text of the file of the request.
wxDECLARE_EVENT(wxEVT_LSP_SEMANTIC_TOKENS, LspSemanticTokensEvent);

// Reads the answer of `textDocument/semanticTokens/full` into the words that it
// names. GUI-free.
class LspSemanticTokens {
public:
  // `serverCapabilities` is the JSON of the capabilities of the server (see
  // LspClient::GetServerCapabilities()): its legend says what the numbers of
  // the tokens mean, and its position encoding must be "utf-8" (the offsets of
  // the tokens are bytes of the text; there is no conversion from UTF-16).
  explicit LspSemanticTokens(const std::string &serverCapabilities)
      : m_serverCapabilities(serverCapabilities) {}

  // `result` is the JSON of the `result` of the answer, `text` is the text that
  // the server had when it answered. An error when the server does not use
  // "utf-8" or the answer is not what it should be.
  StatusOr<LspSemanticWords> GetWords(const std::string &result,
                                      const wxString &text) const;

private:
  // The kind of word for the name of a token type of the server ("class",
  // "function"...): nothing for a type that is not coloured.
  std::optional<LspSemanticType> FindType(const std::string &tokenType) const;

  std::string m_serverCapabilities;
};
