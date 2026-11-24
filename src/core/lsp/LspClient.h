#pragma once

#include "core/Status.h"
#include "core/lsp/JsonRpcFramer.h"
#include "core/lsp/LspServer.h"

#include <string>
#include <wx/event.h>
#include <wx/string.h>

// The client of one language server: it owns the server process (LspServer) and
// the framing of its messages (JsonRpcFramer). GUI-free.
class LspClient : public wxEvtHandler {
public:
  // See LspServer for the arguments.
  LspClient(TransportType type, LspKind kind,
            const wxString &target = wxEmptyString,
            const wxString &user = wxEmptyString);
  ~LspClient() override;

  LspClient(const LspClient &) = delete;
  LspClient &operator=(const LspClient &) = delete;

  // Writes `body` (JSON) with its `Content-Length` header to the server.
  Status Send(const std::string &body);

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
  LspServer m_server;
  // After the server: the thread of the framer calls this client.
  JsonRpcFramer m_framer;
};
