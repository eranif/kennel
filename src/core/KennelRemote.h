#pragma once

#include "core/Process.hpp"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <wx/string.h>

// Wraps the kennel-remote.sh interactive script running on a remote host.
// Manages the lifecycle: upload, start, and send commands over stdin.
// Thread-safe for the output collection; the public methods are NOT reentrant
// (call from one thread at a time — typically the UI thread).
class KennelRemote {
public:
  struct FoundCli {
    wxString name;
    wxString path;
  };

  KennelRemote(const wxString &host, const wxString &user);
  ~KennelRemote();

  // Ensures the script is uploaded and the interactive process is running.
  // Returns false on failure (shows a message box).
  bool EnsureReady();

  // Returns true if the interactive process is alive.
  bool IsRunning() const;

  // Sends a raw command and waits for <<output-end>>. Returns the output
  // (excluding the delimiter), or nullopt on timeout.
  std::optional<std::string> SendCommand(const std::string &cmd,
                                         int timeoutSecs = 10);

  // High-level: list files in a directory.
  // Returns parsed lines (name|size|type). Sets outCwd to the resolved cwd.
  struct FileEntry {
    wxString name;
    size_t size{0};
    bool isDir{false};
  };
  std::vector<FileEntry> ListFiles(const wxString &dir, wxString *outCwd);

  // High-level: find AI CLI tools on the remote machine.
  // Results are cached per host+user for the lifetime of the process.
  std::vector<FoundCli> FindCli();

  // Clears the FindCli cache for all hosts.
  static void ClearCliCache();

  // The SSH connect string (user@host or just host).
  wxString ConnectString() const;

private:
  wxString m_host;
  wxString m_user;
  std::optional<bool> m_scriptUploaded{std::nullopt};
  std::shared_ptr<Process> m_process;

  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::string m_outputBuf;
  bool m_outputComplete{false};

  static std::unordered_map<wxString, std::vector<FoundCli>> s_cliCache;
};
