#include "core/lsp/LspClient.h"

#include "core/EventNotifier.hpp"
#include "core/Logger.h"
#include "core/json.hpp"
#include "core/lsp/LspUri.h"

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
      {"textDocument", {{"synchronization", {{"didSave", true}}}}},
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
  KLOG_DEBUG() << "LSP | a message came, "
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

  if (!hasId || message["id"] != m_initializeId) {
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
