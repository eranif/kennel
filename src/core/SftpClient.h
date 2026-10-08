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
  static StatusOr<RemoteFile> ReadFile(const wxString &host,
                                       const wxString &user,
                                       const wxString &path,
                                       const std::vector<wxString> &searchDirs,
                                       size_t maxBytes = 16 * 1024 * 1024);

  // Replaces the existing remote file at the absolute path `path` with
  // `content`. The data is written to a temporary file next to it, given the
  // original's permissions, and then renamed over it, so a failure part-way
  // never leaves the original truncated. A symlink is followed (its target is
  // replaced). Blocking, like ReadFile.
  static Status WriteFile(const wxString &host, const wxString &user,
                          const wxString &path, const std::string &content);

  // Whether `path` exists on the remote host (a file or a folder). `~/` and
  // `$HOME/` expand to the remote home. Blocking, like ReadFile.
  static StatusOr<bool> Exists(const wxString &host, const wxString &user,
                               const wxString &path);

  // Creates the file at `path`, or replaces it, with `content`; missing parent
  // folders are created. `~/` and `$HOME/` expand to the remote home. Unlike
  // WriteFile this is not atomic, it is meant for small new files. Blocking.
  static Status PutFile(const wxString &host, const wxString &user,
                        const wxString &path, const std::string &content);
};
