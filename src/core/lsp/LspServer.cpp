#include "core/lsp/LspServer.h"

#include "core/Process.hpp"
#include "core/ShellQuote.h"
#include "core/lsp/LspClient.h"

LspServer::LspServer(LspClient *client, TransportType type, LspKind kind,
                     const wxString &target, const wxString &user)
    : m_client(client), m_type(type), m_kind(kind), m_target(target),
      m_user(user) {}

LspServer::~LspServer() { Stop(); }

std::vector<std::string> LspServer::GetServerArgv() const {
  switch (m_kind) {
  case LspKind::kClangd:
    return {"clangd"};
  case LspKind::kGopls:
    return {"gopls"};
  case LspKind::kRustAnalyzer:
    return {"rust-analyzer"};
  }
  return {};
}

std::vector<std::string> LspServer::BuildCommand() const {
  const std::vector<std::string> server = GetServerArgv();
  std::vector<std::string> command;
  switch (m_type) {
  case TransportType::kLocal:
    command = server;
    break;
  case TransportType::kWsl:
    command = {"wsl.exe", "-d", m_target.ToStdString(wxConvUTF8), "--exec"};
    command.insert(command.end(), server.begin(), server.end());
    break;
  case TransportType::kSsh: {
    // The host runs one string with its shell: the words are quoted for it.
    std::string remote;
    for (const std::string &word : server) {
      remote += (remote.empty() ? "" : " ") + ShellQuote(word);
    }
    // -T: no terminal, which would change the bytes. BatchMode: never ask a
    // question (a password, an unknown host key): nobody could answer it, and
    // the server would never start.
    const wxString destination =
        m_user.empty() ? m_target : m_user + "@" + m_target;
    command = {"ssh",
               "-T",
               "-o",
               "BatchMode=yes",
               "-o",
               "ServerAliveInterval=30",
               destination.ToStdString(wxConvUTF8),
               remote};
    break;
  }
  }
  // On Windows the program gets one command line: an argument with a space
  // would fall apart.
  for (std::string &arg : command) {
    arg = Process::QuoteArgument(arg);
  }
  return command;
}

Status LspServer::Start() {
  if (m_process) {
    return Status::Error("The language server was started already");
  }
  m_process = Process::StartInteractive(
      BuildCommand(), [this](const std::string &out, const std::string &err) {
        // On the thread that reads the process: the client gets it on the UI
        // thread.
        if (!out.empty()) {
          m_client->CallAfter(&LspClient::OnServerStdout, out);
        }
        if (!err.empty()) {
          m_client->CallAfter(&LspClient::OnServerStderr, err);
        }
        return true;
      });
  if (!m_process) {
    return Status::Error("The language server could not be started");
  }
  return Status::Ok();
}

Status LspServer::Send(const std::string &bytes) {
  if (!m_process || !m_process->Write(bytes)) {
    return Status::Error("The language server is not running");
  }
  return Status::Ok();
}

void LspServer::Stop() {
  if (m_process) {
    m_process->Stop();
    m_process.reset();
  }
}
