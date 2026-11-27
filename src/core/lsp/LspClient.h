#pragma once

#include "core/IEditor.h"
#include "core/Status.h"
#include "core/lsp/JsonRpcFramer.h"
#include "core/lsp/LspEvent.h"
#include "core/lsp/LspLanguage.h"
#include "core/lsp/LspServer.h"
#include "core/lsp/LspServerKey.h"

#include <map>
#include <optional>
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
// the framing of its messages (JsonRpcFramer), does the handshake, and does all
// that the editors ask: it knows the files that it has open on the server,
// sends what the server lacks of them (didOpen, didChange), asks, and answers
// with an event (see LspSemanticTokensEvent). GUI-free.
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

  // The file of the editor was loaded: the server is told that it is open
  // (didOpen, or didChange when it is open already: a reload). Returns whether
  // the server has the file now: false when the server is not ready, or when
  // the message could not be sent. Nothing is kept; only the first case is
  // tried again by the editor (wxEVT_LSP_SERVER_READY). From here the change
  // counter of the editor says what the server lacks.
  bool NotifyOpened(IEditor *editor);

  // The editor wants the semantic highlights of its text. The changes of the
  // text since NotifyOpened() or the last call are sent first (didChange), then
  // the server is asked. Returns the ID of the request:
  // wxEVT_LSP_SEMANTIC_TOKENS comes later with it. Nothing when the file is not
  // open on the server or the request could not be sent.
  std::optional<RequestID> GetSemanticHighlightsFor(IEditor *editor);

  // The file of the editor is closed, or is going to have another name: the
  // server forgets it. Nothing when it was not open here.
  void CloseDocument(IEditor *editor);
  // The file of the editor was saved: the server is told. Nothing when it is
  // not open here.
  void OnDocumentSaved(IEditor *editor);

  // The process of the server ended (the client is still here): every editor
  // that has a file open on it is told (IEditor::NotifyLspTerminated()), and
  // the client forgets the files and the requests that wait.
  void NotifyTerminated();

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
  // A request that waits for its answer.
  struct Request {
    // What the editor was given, and the editor (IEditor::GetEditorUID()) and
    // the file that it is about.
    RequestID id{0};
    FilePageUID editorId{0};
    Account account;
    wxString path;
    // The change counter of the text that the server had when it was asked.
    uint64_t changeCounter{0};
  };

  // Writes `body` (JSON) with its `Content-Length` header to the server.
  Status Send(const std::string &body);
  // `textDocument/didOpen`: the document at `path` (a path on the machine of
  // the server) is open, and `text` is what it holds now. `language` is the
  // language of the document, `version` is 1 for the first text of a document.
  Status DidOpen(const wxString &path, LspLanguage language, int version,
                 const wxString &text);
  // `textDocument/didChange`: the document at `path` holds `text` now (the
  // whole text), `version` is more than the one that the server knows.
  Status DidChange(const wxString &path, int version, const wxString &text);
  // `textDocument/didSave`: the document at `path` was saved.
  Status DidSave(const wxString &path);
  // `textDocument/didClose`: the document at `path` is closed.
  Status DidClose(const wxString &path);
  // `textDocument/semanticTokens/full` for the document at `path`: the id of
  // the request of the protocol.
  StatusOr<int> RequestSemanticTokens(const wxString &path);
  // The answer to the request `id` of the protocol.
  void OnResponse(int id, const std::string &result, const wxString &error);

  // Sends the request `method` with the JSON text `params`: the id of the
  // request.
  StatusOr<int> SendRequest(const std::string &method,
                            const std::string &params);
  // Sends the notification `method` (a message without an id); `params` is the
  // JSON text of its parameters.
  Status SendNotification(const std::string &method, const std::string &params);
  // Tells the EventNotifier: `type` with the text `text`, from this client.
  void Notify(wxEventType type, const wxString &text);

  LspServerKey m_key;
  LspServer m_server;
  // After the server: the thread of the framer calls this client.
  JsonRpcFramer m_framer;
  // The id of the request `initialize` while it waits for its answer, or -1.
  int m_initializeId{-1};
  // The id of the next request of the protocol (1 is `initialize`).
  int m_nextProtocolId{2};
  std::string m_serverCapabilities;
  // The files that are open on the server, by the ID of their editor: the
  // change counter of the text that the server has. Nothing else of them is
  // kept.
  std::map<FilePageUID, uint64_t> m_changeCounters;
  // The requests that wait for their answer, by their id of the protocol.
  std::map<int, Request> m_requests;
};
