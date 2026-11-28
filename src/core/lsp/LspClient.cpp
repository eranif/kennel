#include "core/lsp/LspClient.h"

#include "core/EventNotifier.hpp"
#include "core/Globals.hpp"
#include "core/IManager.h"
#include "core/Logger.h"
#include "core/json.hpp"
#include "core/lsp/LspSemanticTokens.h"
#include "core/lsp/LspUri.h"

#include <atomic>
#include <utility>
#include <wx/utils.h>

using json = nlohmann::json;

wxDEFINE_EVENT(wxEVT_LSP_CLIENT_READY, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_LSP_CLIENT_FAILED, wxCommandEvent);

LspClient::LspClient(const LspServerKey &key, const wxString &program)
    : m_key(key), m_server(this, key.GetTransportType(), key.m_kind, program,
                           key.m_account.m_name, key.m_account.m_user),
      m_framer(this) {}

LspClient::~LspClient() = default;

Status LspClient::Start() {
  Status started = m_server.Start();
  if (!started.ok()) {
    return started;
  }

  // What this client can do. More is added with the features that use it.
  const json capabilities = {
      {"general", {{"positionEncodings", json::array({"utf-8", "utf-16"})}}},
      {"textDocument",
       {{"synchronization", {{"didSave", true}}},
        {"semanticTokens",
         {{"requests", {{"full", true}}},
          {"tokenTypes",
           json::array({"namespace", "type",     "class",         "enum",
                        "interface", "struct",   "typeParameter", "parameter",
                        "variable",  "property", "enumMember",    "event",
                        "function",  "method",   "macro",         "keyword",
                        "modifier",  "comment",  "string",        "number",
                        "regexp",    "operator", "decorator"})},
          {"tokenModifiers", json::array()},
          {"formats", json::array({"relative"})}}}}},
  };
  const json params = {
      {"processId", static_cast<int>(wxGetProcessId())},
      {"clientInfo", {{"name", "Kennel"}}},
      {"rootUri",
       LspUri(m_key.m_account.m_rootDir).GetUri().ToStdString(wxConvUTF8)},
      {"capabilities", capabilities},
  };
  m_initializeId = 1;
  KLOG_DEBUG() << "LSP | sending initialize, root: "
               << params["rootUri"].get<std::string>();
  const json request = {{"jsonrpc", "2.0"},
                        {"id", m_initializeId},
                        {"method", "initialize"},
                        {"params", params}};
  return Send(request.dump());
}

Status LspClient::Send(const std::string &body) {
  return m_server.Send("Content-Length: " + std::to_string(body.size()) +
                       "\r\n\r\n" + body);
}

Status LspClient::SendNotification(const std::string &method,
                                   const std::string &params) {
  // The envelope is written by hand: `params` holds the whole text of a file
  // and is not parsed again.
  return Send("{\"jsonrpc\":\"2.0\",\"method\":" + json(method).dump() +
              ",\"params\":" + params + "}");
}

Status LspClient::DidOpen(const wxString &path, LspLanguage language,
                          int version, const wxString &text) {
  // The name that the protocol gives to the language.
  std::string languageId;
  switch (language) {
  case LspLanguage::kC:
    languageId = "c";
    break;
  case LspLanguage::kObjectiveC:
    languageId = "objective-c";
    break;
  case LspLanguage::kObjectiveCpp:
    languageId = "objective-cpp";
    break;
  case LspLanguage::kGo:
    languageId = "go";
    break;
  case LspLanguage::kRust:
    languageId = "rust";
    break;
  case LspLanguage::kPython:
    languageId = "python";
    break;
  case LspLanguage::kPhp:
    languageId = "php";
    break;
  case LspLanguage::kCpp:
  case LspLanguage::kNone:
    languageId = "cpp";
    break;
  }
  const json params = {{"textDocument",
                        {{"uri", LspUri(path).GetUri().ToStdString(wxConvUTF8)},
                         {"languageId", languageId},
                         {"version", version},
                         {"text", text.ToStdString(wxConvUTF8)}}}};
  KLOG_DEBUG() << "LSP | didOpen " << path;
  return SendNotification("textDocument/didOpen", params.dump());
}

Status LspClient::DidChange(const wxString &path, int version,
                            const wxString &text) {
  // The whole text: one change without a range.
  const json params = {
      {"textDocument",
       {{"uri", LspUri(path).GetUri().ToStdString(wxConvUTF8)},
        {"version", version}}},
      {"contentChanges",
       json::array({json{{"text", text.ToStdString(wxConvUTF8)}}})}};
  KLOG_DEBUG() << "LSP | didChange " << path << ", version " << version;
  return SendNotification("textDocument/didChange", params.dump());
}

StatusOr<int> LspClient::RequestSemanticTokens(const wxString &path) {
  const json params = {
      {"textDocument",
       {{"uri", LspUri(path).GetUri().ToStdString(wxConvUTF8)}}}};
  KLOG_DEBUG() << "LSP | semanticTokens/full " << path;
  return SendRequest("textDocument/semanticTokens/full", params.dump());
}

StatusOr<int> LspClient::SendRequest(const std::string &method,
                                     const std::string &params) {
  const int id = m_nextProtocolId++;
  const Status sent = Send("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                           ",\"method\":" + json(method).dump() +
                           ",\"params\":" + params + "}");
  if (!sent.ok()) {
    return sent;
  }
  return id;
}

bool LspClient::NotifyOpened(IEditor *editor) {
  // Until the handshake is done the server is not ready for anything.
  if (editor == nullptr || m_serverCapabilities.empty()) {
    return false;
  }
  const wxString path = editor->GetFilePath();
  const uint64_t changeCounter = editor->GetChangeCounter();
  // The change counter is the version of the text: it goes up with each change
  // and never goes back.
  const int version = static_cast<int>(changeCounter);
  const FilePageUID editorId = editor->GetEditorUID();
  const bool isOpen = m_changeCounters.count(editorId) != 0;
  // A file that is loaded again (a reload) is open already: its text is sent as
  // a change, a document is opened once.
  const Status sent = isOpen ? DidChange(path, version, editor->GetText())
                             : DidOpen(path, editor->GetLanguageId(), version,
                                       editor->GetText());
  if (!sent.ok()) {
    KLOG_WARN() << "LSP | " << path << ": " << sent.message();
    return false;
  }
  m_changeCounters[editorId] = changeCounter;
  return true;
}

std::optional<RequestID> LspClient::GetSemanticHighlightsFor(IEditor *editor) {
  if (editor == nullptr) {
    return std::nullopt;
  }
  const wxString path = editor->GetFilePath();
  const uint64_t changeCounter = editor->GetChangeCounter();

  // The server has the file since NotifyOpened(), or it is not ready.
  const auto known = m_changeCounters.find(editor->GetEditorUID());
  if (known == m_changeCounters.end()) {
    return std::nullopt;
  }
  // The changes since: before the request, the server answers for the text that
  // it has.
  if (known->second != changeCounter) {
    const Status changed =
        DidChange(path, static_cast<int>(changeCounter), editor->GetText());
    if (!changed.ok()) {
      KLOG_WARN() << "LSP | didChange " << path << ": " << changed.message();
      return std::nullopt;
    }
    known->second = changeCounter;
  }

  const StatusOr<int> sent = RequestSemanticTokens(path);
  if (!sent.ok()) {
    KLOG_WARN() << "LSP | semantic tokens " << path << ": "
                << sent.status().message();
    return std::nullopt;
  }
  // One ID for all the clients: an editor may wait for answers of several.
  static std::atomic<RequestID> nextRequestId{1};
  Request request;
  request.id = nextRequestId++;
  request.editorId = editor->GetEditorUID();
  request.account = editor->GetAccount();
  request.path = path;
  request.changeCounter = changeCounter;
  m_requests.emplace(*sent, request);
  return request.id;
}

void LspClient::OnResponse(int id, const std::string &result,
                           const wxString &error) {
  const auto found = m_requests.find(id);
  if (found == m_requests.end()) {
    return;
  }
  const Request request = found->second;
  m_requests.erase(found);
  if (!error.empty()) {
    KLOG_WARN() << "LSP | " << request.path << ": " << error;
    return;
  }
  // The editor is fetched from the live application, if it is still there.
  IManager *manager = GetKennelManager();
  IEditor *editor =
      manager ? manager->FindEditorByUID(request.editorId) : nullptr;
  if (editor == nullptr ||
      editor->GetChangeCounter() != request.changeCounter) {
    // The file was closed, or its text changed since the server was asked: the
    // answer is for the old one. The editor asks again for the new one.
    KLOG_DEBUG() << "LSP | " << request.path
                 << ": the semantic tokens are old, dropped";
    return;
  }
  const StatusOr<LspSemanticWords> words =
      LspSemanticTokens(m_serverCapabilities)
          .GetWords(result, editor->GetText());
  if (!words.ok()) {
    KLOG_WARN() << "LSP | " << request.path << ": " << words.status().message();
    return;
  }
  KLOG_DEBUG() << "LSP | " << request.path << ": semantic tokens";
  LspSemanticTokensEvent tokens(wxEVT_LSP_SEMANTIC_TOKENS);
  tokens.SetRequestId(request.id);
  tokens.SetAccount(request.account);
  tokens.SetFilePath(request.path);
  tokens.SetWords(*words);
  EventNotifier::Get()->ProcessEvent(tokens);
}

void LspClient::NotifyTerminated() {
  // The process is dead, but the manager notices it once a second: an editor
  // may have called this client since. What it was told is cleared here too.
  IManager *manager = GetKennelManager();
  for (const auto &opened : m_changeCounters) {
    if (IEditor *editor =
            manager ? manager->FindEditorByUID(opened.first) : nullptr) {
      editor->NotifyLspTerminated();
    }
  }
  m_changeCounters.clear();
  m_requests.clear();
}

void LspClient::CloseDocument(IEditor *editor) {
  if (editor == nullptr ||
      m_changeCounters.erase(editor->GetEditorUID()) == 0) {
    return; // Not open here
  }
  const wxString path = editor->GetFilePath();
  const Status closed = DidClose(path);
  if (!closed.ok()) {
    KLOG_WARN() << "LSP | didClose " << path << ": " << closed.message();
  }
}

void LspClient::OnDocumentSaved(IEditor *editor) {
  if (editor == nullptr ||
      m_changeCounters.count(editor->GetEditorUID()) == 0) {
    return; // Not open here
  }
  const wxString path = editor->GetFilePath();
  const Status saved = DidSave(path);
  if (!saved.ok()) {
    KLOG_WARN() << "LSP | didSave " << path << ": " << saved.message();
  }
}

Status LspClient::DidSave(const wxString &path) {
  const json params = {
      {"textDocument",
       {{"uri", LspUri(path).GetUri().ToStdString(wxConvUTF8)}}}};
  KLOG_DEBUG() << "LSP | didSave " << path;
  return SendNotification("textDocument/didSave", params.dump());
}

Status LspClient::DidClose(const wxString &path) {
  const json params = {
      {"textDocument",
       {{"uri", LspUri(path).GetUri().ToStdString(wxConvUTF8)}}}};
  KLOG_DEBUG() << "LSP | didClose " << path;
  return SendNotification("textDocument/didClose", params.dump());
}

void LspClient::Notify(wxEventType type, const wxString &text) {
  wxCommandEvent event(type);
  event.SetEventObject(this);
  event.SetString(text);
  EventNotifier::Get()->AddPendingEvent(event);
}

void LspClient::OnServerStdout(std::string buffer) {
  m_framer.Feed(std::move(buffer));
}

void LspClient::OnServerStderr(std::string buffer) {
  KLOG_DEBUG() << "LSP | " << buffer;
}

void LspClient::OnParsed(std::string body) {
  const json message = json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (!message.is_object()) {
    KLOG_WARN() << "LSP | a message that is not JSON";
    return;
  }
  const bool hasId = message.contains("id");
  KLOG_TRACE() << "LSP | a message came, "
               << (message.contains("method") && message["method"].is_string()
                       ? message["method"].get<std::string>()
                       : std::string("an answer"));

  if (message.contains("method")) {
    // A request of the server must be answered (the server may wait for the
    // answer): progress, registering a capability, the configuration. None of
    // them has something to say yet.
    if (hasId) {
      const bool isConfiguration =
          message["method"].is_string() &&
          message["method"] == "workspace/configuration";
      const json reply = {{"jsonrpc", "2.0"},
                          {"id", message["id"]},
                          {"result", isConfiguration ? json::array() : json()}};
      Send(reply.dump());
    }
    // A notification of the server: nothing uses them yet.
    return;
  }

  if (!hasId) {
    return;
  }
  if (message["id"] != m_initializeId) {
    // The answer to a request of an editor.
    if (message["id"].is_number_integer()) {
      wxString error;
      if (message.contains("error")) {
        const json &failure = message["error"];
        error = failure.is_object() && failure.contains("message") &&
                        failure["message"].is_string()
                    ? wxString::FromUTF8(failure["message"].get<std::string>())
                    : wxString("The request failed");
      }
      OnResponse(message["id"].get<int>(),
                 message.contains("result") ? message["result"].dump() : "null",
                 error);
    }
    return;
  }

  // The answer to `initialize`.
  KLOG_DEBUG() << "LSP | the answer to initialize came";
  m_initializeId = -1;
  if (message.contains("error")) {
    const json &error = message["error"];
    wxString why = "initialize failed";
    if (error.is_object() && error.contains("message") &&
        error["message"].is_string()) {
      why = wxString::FromUTF8(error["message"].get<std::string>());
    }
    Notify(wxEVT_LSP_CLIENT_FAILED, why);
    return;
  }
  const json &result = message.contains("result") ? message["result"] : json();
  m_serverCapabilities =
      result.contains("capabilities") ? result["capabilities"].dump() : "{}";

  const json initialized = {{"jsonrpc", "2.0"},
                            {"method", "initialized"},
                            {"params", json::object()}};
  const Status sent = Send(initialized.dump());
  if (!sent.ok()) {
    Notify(wxEVT_LSP_CLIENT_FAILED, sent.message());
    return;
  }
  KLOG_DEBUG() << "LSP | sent initialized; capabilities: "
               << m_serverCapabilities.size() << " bytes";
  Notify(wxEVT_LSP_CLIENT_READY, wxEmptyString);
}
