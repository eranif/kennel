#include "core/lsp/LspTransport.h"

#include "core/EventNotifier.hpp"
#include "core/Process.hpp"
#include "core/ShellQuote.h"

wxDEFINE_EVENT(wxEVT_LSP_TRANSPORT_OPENED, LspTransportEvent);
wxDEFINE_EVENT(wxEVT_LSP_TRANSPORT_STDOUT, LspTransportEvent);
wxDEFINE_EVENT(wxEVT_LSP_TRANSPORT_STDERR, LspTransportEvent);
wxDEFINE_EVENT(wxEVT_LSP_TRANSPORT_CLOSED, LspTransportEvent);

LspTransport::LspTransport(TransportType type, LspKind kind,
                           const wxString &target, const wxString &user)
    : m_type(type), m_kind(kind), m_target(target), m_user(user),
      m_timer(this) {
  Bind(wxEVT_TIMER, &LspTransport::OnTimer, this);
}

LspTransport::~LspTransport() { Stop(); }

std::vector<std::string> LspTransport::GetServerArgv() const {
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

std::vector<std::string> LspTransport::BuildCommand() const {
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

Status LspTransport::Start() {
  if (m_process) {
    return Status::Error("The language server was started already");
  }
  m_process = Process::StartInteractive(
      BuildCommand(), [this](const std::string &out, const std::string &err) {
        // On the thread that reads the process: the events go to the UI thread.
        if (!out.empty()) {
          LspTransportEvent event(wxEVT_LSP_TRANSPORT_STDOUT);
          event.SetEventObject(this);
          event.SetData(out);
          EventNotifier::Get()->AddPendingEvent(event);
        }
        if (!err.empty()) {
          LspTransportEvent event(wxEVT_LSP_TRANSPORT_STDERR);
          event.SetEventObject(this);
          event.SetData(err);
          EventNotifier::Get()->AddPendingEvent(event);
        }
        return true;
      });
  if (!m_process) {
    return Status::Error("The language server could not be started");
  }
  LspTransportEvent opened(wxEVT_LSP_TRANSPORT_OPENED);
  opened.SetEventObject(this);
  EventNotifier::Get()->AddPendingEvent(opened);
  m_timer.Start(250);
  return Status::Ok();
}

Status LspTransport::Send(const std::string &bytes) {
  if (!m_process || !m_process->Write(bytes)) {
    return Status::Error("The language server is not running");
  }
  return Status::Ok();
}

void LspTransport::Stop() {
  m_timer.Stop();
  if (m_process) {
    m_process->Stop();
    m_process.reset();
  }
}

void LspTransport::OnTimer(wxTimerEvent &) {
  if (!m_process || m_process->IsRunning()) {
    return;
  }
  // It ended by itself: Process has delivered all its output by now.
  m_timer.Stop();
  m_process.reset();
  LspTransportEvent event(wxEVT_LSP_TRANSPORT_CLOSED);
  event.SetEventObject(this);
  event.SetExitCode(-1);
  EventNotifier::Get()->AddPendingEvent(event);
}
