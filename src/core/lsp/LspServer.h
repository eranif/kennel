#pragma once

#include "core/Status.h"

#include <memory>
#include <string>
#include <vector>
#include <wx/string.h>

class LspClient;
class Process;

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

// One language server: its process, its input and its output, wherever it
// runs. The server is always a process of this machine; for WSL and ssh that
// process is the program that carries the pipes to the other machine (wsl.exe,
// ssh).
//
// It moves bytes and nothing else: the framing of the messages is not its job.
// What the server writes to its standard output and standard error is given to
// the client with CallAfter(), so the client gets it on the UI thread.
// GUI-free.
class LspServer {
public:
  // `client` gets the output of the server, and must outlive this object.
  // `program` is the server to run, as a path on the machine where it runs; for
  // kSsh it may start with "$HOME/", which the shell of the host expands. Empty
  // means the name of the program of `kind`, found on the PATH. `target` is the
  // WSL distribution (kWsl) or the host (kSsh), which may be an ~/.ssh/config
  // alias; empty for kLocal. `user` is for kSsh only: empty means the ssh
  // default.
  LspServer(LspClient *client, TransportType type, LspKind kind,
            const wxString &program = wxEmptyString,
            const wxString &target = wxEmptyString,
            const wxString &user = wxEmptyString);
  // Ends the server (see Stop()).
  ~LspServer();

  LspServer(const LspServer &) = delete;
  LspServer &operator=(const LspServer &) = delete;

  // Starts the server and returns at once. An error when it could not be
  // started, or was started before.
  Status Start();

  // Whether the process of the server is alive.
  bool IsRunning() const;

  // Writes `bytes` to the input of the server. An error when it is not running.
  Status Send(const std::string &bytes);

  // Ends the server.
  void Stop();

private:
  // The server and its arguments, for the kind of the server.
  std::vector<std::string> GetServerArgv() const;
  // The command line of the process to start: the server itself, or wsl.exe or
  // ssh with the server as their command. Every argument is quoted for the
  // command line of a program (see Process::QuoteArgument()).
  std::vector<std::string> BuildCommand() const;
  // `word` as one word of the shell of a host: quoted, except a leading
  // "$HOME/", which the shell must expand.
  std::string QuoteForHostShell(const std::string &word) const;

  LspClient *m_client;
  TransportType m_type;
  LspKind m_kind;
  wxString m_program;
  wxString m_target;
  wxString m_user;
  std::shared_ptr<Process> m_process;
};
