#pragma once

#include "core/Status.h"
#include "core/lsp/LspServer.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <wx/event.h>

// One message of the server: the JSON, without its header. Sent on the
// EventNotifier, on the UI thread. The event object (GetEventObject()) is the
// LspServer that the message came from. GUI-free.
class JsonRpcEvent : public wxCommandEvent {
public:
  explicit JsonRpcEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  JsonRpcEvent(const JsonRpcEvent &) = default;
  wxEvent *Clone() const override { return new JsonRpcEvent(*this); }

  const std::string &GetBody() const { return m_body; }
  void SetBody(const std::string &body) { m_body = body; }

private:
  std::string m_body;
};

// A whole message came from the server.
wxDECLARE_EVENT(wxEVT_JSON_RPC_MESSAGE, JsonRpcEvent);

// The framing of JSON-RPC in the Language Server Protocol: every message is
// `Content-Length: <n>\r\n\r\n` and then n bytes of JSON (UTF-8).
//
// It works for one LspServer. Incoming: it listens to the output of the
// transport (wxEVT_LSP_SERVER_STDOUT on the EventNotifier), cuts it into
// messages on a thread of its own (a long message must not stall the UI), and
// the UI thread sends each one as a wxEVT_JSON_RPC_MESSAGE. Outgoing: Send()
// puts the header on a message and writes it to the transport.
class JsonRpcFramer : public wxEvtHandler {
public:
  // A message that claims to be longer is not a message: its header is skipped.
  static constexpr size_t kMaxMessageBytes = 256 * 1024 * 1024;

  // `transport` must outlive the framer.
  explicit JsonRpcFramer(LspServer *transport);
  // Stops the thread and waits for it.
  ~JsonRpcFramer() override;

  JsonRpcFramer(const JsonRpcFramer &) = delete;
  JsonRpcFramer &operator=(const JsonRpcFramer &) = delete;

  // Writes `body` (JSON) with its header to the server.
  Status Send(const std::string &body);

private:
  // The output of a transport: the bytes go to the thread when it is ours.
  void OnStdout(LspServerEvent &event);
  // The thread found a message: the UI thread sends it (the thread calls this
  // with CallAfter()).
  void OnParsed(std::string body);

  // The thread: waits for bytes, and hands each whole message to OnParsed() on
  // the UI thread. It is started with `this`, which is valid for as long as it
  // runs: the destructor joins it.
  void Run();
  // Takes the next whole message out of `m_buffer`, or nothing when there is
  // none yet. Text that is not a message (what a wrapper program prints before
  // the server starts) is skipped, so that the next message is still found.
  // Only the thread calls it.
  std::optional<std::string> ExtractMessage();

  LspServer *m_transport;

  std::mutex m_mutex;
  std::condition_variable m_wake;
  // Bytes that came and the thread did not take yet. Guarded by `m_mutex`.
  std::string m_incoming;
  // The thread must end. Guarded by `m_mutex`.
  bool m_stop{false};
  // What the thread took and is not a whole message yet. Only the thread uses
  // it.
  std::string m_buffer;
  std::thread m_worker;
};
