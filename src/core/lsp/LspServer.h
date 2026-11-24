#pragma once

#include "core/Status.h"

#include <memory>
#include <string>
#include <vector>
#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

class Process;

// What a transport reports. Sent on the EventNotifier, on the UI thread. The
// event object (GetEventObject()) is the LspServer that sent it.
// GUI-free.
class LspServerEvent : public wxCommandEvent {
public:
  explicit LspServerEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  LspServerEvent(const LspServerEvent &) = default;
  wxEvent *Clone() const override { return new LspServerEvent(*this); }

  // wxEVT_LSP_SERVER_STDOUT and wxEVT_LSP_SERVER_STDERR: raw bytes, in
  // the order they came. They may end in the middle of a message.
  const std::string &GetData() const { return m_data; }
  void SetData(const std::string &data) { m_data = data; }

  // wxEVT_LSP_SERVER_CLOSED: an error status when the server could not be
  // started or the connection was lost, ok when the server ended by itself.
  const Status &GetResult() const { return m_result; }
  void SetResult(const Status &result) { m_result = result; }
  // wxEVT_LSP_SERVER_CLOSED: the exit code of the server, -1 when there is
  // none.
  int GetExitCode() const { return GetInt(); }
  void SetExitCode(int exitCode) { SetInt(exitCode); }

private:
  std::string m_data;
  Status m_result;
};

// The server runs and its input can be written.
wxDECLARE_EVENT(wxEVT_LSP_SERVER_OPENED, LspServerEvent);
// The server wrote to its standard output (the messages) or its standard error
// (its log). See LspServerEvent::GetData().
wxDECLARE_EVENT(wxEVT_LSP_SERVER_STDOUT, LspServerEvent);
wxDECLARE_EVENT(wxEVT_LSP_SERVER_STDERR, LspServerEvent);
// Sent once, last, unless Stop() was called first.
wxDECLARE_EVENT(wxEVT_LSP_SERVER_CLOSED, LspServerEvent);

// Where the language server runs.
enum class TransportType {
  kWsl,   // In a WSL distribution: wsl.exe -d <distribution> --exec <server>
  kLocal, // On this machine: <server>
  kSsh,   // On a host: ssh <host> <server>
};

// Which language server it is.
enum class LspKind {
  kClangd,       // C and C++
  kGopls,        // Go
  kRustAnalyzer, // Rust
};

// The pipe to one language server: its input and its output, wherever it runs.
// The server is always a process of this machine; for WSL and ssh that process
// is the program that carries the pipes to the other machine (wsl.exe, ssh).
//
// It moves bytes and nothing else: the framing of the messages is not its job.
// It never calls back: everything it has to say is an LspServerEvent, sent
// on the EventNotifier.
class LspServer : public wxEvtHandler {
public:
  // `target` is the WSL distribution (kWsl) or the host (kSsh), which may be an
  // ~/.ssh/config alias; empty for kLocal. `user` is for kSsh only: empty means
  // the ssh default.
  LspServer(TransportType type, LspKind kind,
            const wxString &target = wxEmptyString,
            const wxString &user = wxEmptyString);
  // Ends the server (see Stop()).
  ~LspServer() override;

  // Starts the server and returns at once. wxEVT_LSP_SERVER_OPENED comes
  // when it runs, and wxEVT_LSP_SERVER_CLOSED when it ended. An error (and
  // no event) when it could not be started, or was started before.
  Status Start();

  // Writes `bytes` to the input of the server. An error when it is not running.
  Status Send(const std::string &bytes);

  // Ends the server. No event is sent after this.
  void Stop();

private:
  // The server and its arguments, for the kind of the server.
  std::vector<std::string> GetServerArgv() const;
  // The command line of the process to start: the server itself, or wsl.exe or
  // ssh with the server as their command. Every argument is quoted for the
  // command line of a program (see Process::QuoteArgument()).
  std::vector<std::string> BuildCommand() const;
  // Looks whether the process is still there: Process says nothing when it
  // ends.
  void OnTimer(wxTimerEvent &event);

  TransportType m_type;
  LspKind m_kind;
  wxString m_target;
  wxString m_user;
  std::shared_ptr<Process> m_process;
  wxTimer m_timer;
};
