#include "core/lsp/LspServer.h"

#include "core/Logger.h"
#include "core/Process.hpp"
#include "core/ShellQuote.h"
#include "core/lsp/LspClient.h"

LspServer::LspServer(LspClient *client, TransportType type, LspKind kind,
                     const wxString &program, const wxString &target,
                     const wxString &user)
    : m_client(client), m_type(type), m_kind(kind), m_program(program),
      m_target(target), m_user(user) {}

LspServer::~LspServer() { Stop(); }

std::string LspServer::QuoteForHostShell(const std::string &word) const {
  static const std::string kHome = "$HOME/";
  if (word.compare(0, kHome.size(), kHome) == 0) {
    return "\"$HOME\"/" + ShellQuote(word.substr(kHome.size()));
  }
  return ShellQuote(word);
}

std::vector<std::string> LspServer::GetServerArgv() const {
  // The program (by its name, found on the PATH) and the arguments that make it
  // a server on its standard input and output.
  std::vector<std::string> argv;
  switch (m_kind) {
  case LspKind::kClangd:
    argv = {"clangd"};
    break;
  case LspKind::kGopls:
    argv = {"gopls"};
    break;
  case LspKind::kRustAnalyzer:
    argv = {"rust-analyzer"};
    break;
  case LspKind::kPyright:
    argv = {"pyright-langserver", "--stdio"};
    break;
  case LspKind::kPhpantom:
    argv = {"phpantom_lsp", "--stdio"};
    break;
  }
  // The program that was given (a full path) takes the place of its name.
  if (!m_program.empty() && !argv.empty()) {
    argv.front() = m_program.ToStdString(wxConvUTF8);
  }
  return argv;
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
      remote += (remote.empty() ? "" : " ") + QuoteForHostShell(word);
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
  const std::vector<std::string> command = BuildCommand();
  std::string commandLine;
  for (const std::string &arg : command) {
    commandLine += (commandLine.empty() ? "" : " ") + arg;
  }
  KLOG_DEBUG() << "LSP | starting: " << commandLine;
  m_process = Process::StartInteractive(
      command, [this](const std::string &out, const std::string &err) {
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
    KLOG_DEBUG() << "LSP | the process did not start";
    return Status::Error("The language server could not be started");
  }
  KLOG_DEBUG() << "LSP | the process is running";
  return Status::Ok();
}

bool LspServer::IsRunning() const {
  return m_process && m_process->IsRunning();
}

Status LspServer::Send(const std::string &bytes) {
  if (!m_process || !m_process->Write(bytes)) {
    return Status::Error("The language server is not running");
  }
  return Status::Ok();
}

void LspServer::Stop() {
  if (m_process) {
    const std::vector<std::string> argv = GetServerArgv();
    const std::string program = argv.empty() ? std::string() : argv.front();
    KLOG_DEBUG() << "LSP | stopping " << program
                 << (m_target.empty() ? wxString() : " on " + m_target);
    m_process->Stop();
    m_process.reset();
    KLOG_DEBUG() << "LSP | stopped " << program
                 << (m_target.empty() ? wxString() : " on " + m_target);
  }
}
