#pragma once

#include "core/Status.h"

#include <string>
#include <vector>
#include <wx/string.h>

// Reads files off a remote host over SFTP (libssh). Blocking: call it from a
// worker thread, never from the UI thread. GUI-free; libssh stays out of this
// header.
class SftpClient {
public:
  struct RemoteFile {
    wxString path;       // The resolved absolute remote path
    std::string content; // Raw bytes
  };

  // Connects to `host` (as `user`, when non-empty), locates `path` and reads
  // it. `host` may be an ~/.ssh/config alias; the config is honoured.
  //
  // Absolute paths are used as-is and `~/` expands to the remote home. Any
  // other path is tried relative to each entry of `searchDirs` in order
  // (`$HOME`/`~` are expanded), then relative to the remote home.
  //
  // Authentication is key-based only (ssh-agent, then the default keys in
  // ~/.ssh), the same requirement as Kennel's other remote features. Files
  // larger than `maxBytes` are rejected.
  static StatusOr<RemoteFile>
  ReadFile(const wxString &host, const wxString &user, const wxString &path,
           const std::vector<wxString> &searchDirs,
           size_t maxBytes = 16 * 1024 * 1024);
};
