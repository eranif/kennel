#include "core/lsp/LspClient.h"

#include "core/Logger.h"

#include <utility>

LspClient::LspClient(TransportType type, LspKind kind, const wxString &target,
                     const wxString &user)
    : m_server(this, type, kind, target, user), m_framer(this) {}

LspClient::~LspClient() = default;

Status LspClient::Send(const std::string &body) {
  return m_server.Send("Content-Length: " + std::to_string(body.size()) +
                       "\r\n\r\n" + body);
}

void LspClient::OnServerStdout(std::string buffer) {
  m_framer.Feed(std::move(buffer));
}

void LspClient::OnServerStderr(std::string buffer) {
  KLOG_DEBUG() << "Language server: " << buffer;
}

void LspClient::OnParsed(std::string body) { wxUnusedVar(body); }
