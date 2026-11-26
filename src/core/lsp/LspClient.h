#pragma once

#include "core/Status.h"
#include "core/lsp/JsonRpcFramer.h"
#include "core/lsp/LspServer.h"
#include "core/lsp/LspServerKey.h"

#include <string>
#include <wx/event.h>
#include <wx/string.h>

// Sent on the EventNotifier when the handshake with the server is done: the
// server can be used. GetEventObject() is the LspClient.
wxDECLARE_EVENT(wxEVT_LSP_CLIENT_READY, wxCommandEvent);
// Sent on the EventNotifier when the server did not accept the handshake.
// GetEventObject() is the LspClient, GetString() says why.
wxDECLARE_EVENT(wxEVT_LSP_CLIENT_FAILED, wxCommandEvent);

// The client of one language server: it owns the server process (LspServer) and
// the framing of its messages (JsonRpcFramer), and does the handshake.
// GUI-free.
class LspClient : public wxEvtHandler {
public:
  // `key` says which server, and where it runs. `program` is what LspServer
  // runs (see LspServerKey::GetProgram()).
  LspClient(const LspServerKey &key, const wxString &program);
  ~LspClient() override;

  LspClient(const LspClient &) = delete;
  LspClient &operator=(const LspClient &) = delete;

  // Starts the server and the handshake (`initialize`, then `initialized` when
  // the server answers). Returns at once: wxEVT_LSP_CLIENT_READY or
  // wxEVT_LSP_CLIENT_FAILED comes later. An error when the server could not be
  // started.
  Status Start();

  // Whether the process of the server is alive.
  bool IsRunning() const { return m_server.IsRunning(); }

  // Writes `body` (JSON) with its `Content-Length` header to the server.
  Status Send(const std::string &body);

  // What the server said it can do (the `capabilities` of its answer to
  // `initialize`) as JSON text. Empty until wxEVT_LSP_CLIENT_READY.
  const std::string &GetServerCapabilities() const {
    return m_serverCapabilities;
  }

  // A whole message came from the server. The thread of the JsonRpcFramer calls
  // it with CallAfter(), so it runs on the UI thread.
  void OnParsed(std::string body);

  // The server wrote to its standard output. The thread that reads the server
  // calls it with CallAfter(), so it runs on the UI thread.
  void OnServerStdout(std::string buffer);
  // The server wrote to its standard error (its log). Called like
  // OnServerStdout().
  void OnServerStderr(std::string buffer);

private:
  // Tells the EventNotifier: `type` with the text `text`, from this client.
  void Notify(wxEventType type, const wxString &text);

  LspServerKey m_key;
  LspServer m_server;
  // After the server: the thread of the framer calls this client.
  JsonRpcFramer m_framer;
  // The id of the request `initialize` while it waits for its answer, or -1.
  int m_initializeId{-1};
  std::string m_serverCapabilities;
};
