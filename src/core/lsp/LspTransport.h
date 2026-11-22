#pragma once

#include "core/Status.h"

#include <string>
#include <vector>
#include <wx/event.h>

// What a transport reports. Sent to the handler given to LspTransport::Start(),
// on its thread (the UI thread). GUI-free.
class LspTransportEvent : public wxCommandEvent {
public:
  explicit LspTransportEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  LspTransportEvent(const LspTransportEvent &) = default;
  wxEvent *Clone() const override { return new LspTransportEvent(*this); }

  // wxEVT_LSP_TRANSPORT_STDOUT and wxEVT_LSP_TRANSPORT_STDERR: raw bytes, in
  // the order they came. They may end in the middle of a message.
  const std::string &GetData() const { return m_data; }
  void SetData(const std::string &data) { m_data = data; }

  // wxEVT_LSP_TRANSPORT_CLOSED: an error status when the server could not be
  // started or the connection was lost, ok when the server ended by itself.
  const Status &GetResult() const { return m_result; }
  void SetResult(const Status &result) { m_result = result; }
  // wxEVT_LSP_TRANSPORT_CLOSED: the exit code of the server, -1 when there is
  // none.
  int GetExitCode() const { return GetInt(); }
  void SetExitCode(int exitCode) { SetInt(exitCode); }

private:
  std::string m_data;
  Status m_result;
};

// The server runs and its input can be written.
wxDECLARE_EVENT(wxEVT_LSP_TRANSPORT_OPENED, LspTransportEvent);
// The server wrote to its standard output (the messages) or its standard error
// (its log). See LspTransportEvent::GetData().
wxDECLARE_EVENT(wxEVT_LSP_TRANSPORT_STDOUT, LspTransportEvent);
wxDECLARE_EVENT(wxEVT_LSP_TRANSPORT_STDERR, LspTransportEvent);
// Sent once, last, unless Stop() was called first.
wxDECLARE_EVENT(wxEVT_LSP_TRANSPORT_CLOSED, LspTransportEvent);

// The pipe to one language server: its input and its output, wherever it runs
// (this machine, a WSL distribution, an ssh host). One implementation for each
// kind of place.
//
// It moves bytes and nothing else: the framing of the messages is not its job.
// It never calls back: everything it has to say is an LspTransportEvent, sent
// to the handler that was given to Start().
class LspTransport {
public:
  virtual ~LspTransport() = default;

  // Starts `argv` (the program, then its arguments) and returns at once.
  // wxEVT_LSP_TRANSPORT_OPENED comes when it runs, and
  // wxEVT_LSP_TRANSPORT_CLOSED when it ended or could not be started. An error
  // (and no event) when it can not even be tried, or was started before.
  // `handler` must outlive the transport, or Stop() must be called first.
  virtual Status Start(const std::vector<std::string> &argv,
                       wxEvtHandler *handler) = 0;

  // Queues `bytes` for the input of the server. Never blocks the caller.
  virtual void Send(const std::string &bytes) = 0;

  // Ends the server. No event is sent after this.
  virtual void Stop() = 0;
};
