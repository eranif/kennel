#pragma once

#include "core/Status.h"

#include <atomic>
#include <map>
#include <string>
#include <vector>
#include <wx/string.h>

// Reads files off a remote host over SFTP (libssh). Blocking: call it from a
// worker thread, never from the UI thread. GUI-free; libssh stays out of this
// header.
//
// The connections are kept open in a pool, one for each host and user, and
// reused by the next call for them. A call never shares a connection with
// another call that runs at the same time. Only a connection whose call worked
// goes back to the pool, and one that was idle for more than 5 minutes is not
// reused. When a host closed a connection while it was idle, the call is made
// again on a new connection.
class SftpClient {
public:
  struct RemoteFile {
    wxString path;       // The resolved absolute remote path
    std::string content; // Raw bytes
  };

  // Connects to `host` (as `user`, when non-empty), locates `path` and reads
  // it. `host` may be an ~/.ssh/config alias; the config is honoured.
  //
  // Absolute paths are used as-is and `~/` and `$HOME/` expand to the remote
  // home. Any
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

  // The start of the first line (at most `maxBytes` bytes, no line end) of each
  // of the files `paths` (absolute remote paths), on one connection. A path
  // that cannot be read (it is not a file, no permission) is not in the answer;
  // that is not an error. The error is only for a host that cannot be reached.
  // When `cancel` is set and becomes true, it stops and returns what it has.
  // Blocking, like ReadFile.
  static StatusOr<std::map<wxString, wxString>>
  ReadFirstLines(const wxString &host, const wxString &user,
                 const std::vector<wxString> &paths,
                 const std::atomic<bool> *cancel = nullptr,
                 size_t maxBytes = 256);

  // Replaces the existing remote file at the absolute path `path` with
  // `content`. The data is written to a temporary file next to it, given the
  // original's permissions, and then renamed over it, so a failure part-way
  // never leaves the original truncated. A symlink is followed (its target is
  // replaced). Blocking, like ReadFile.
  static Status WriteFile(const wxString &host, const wxString &user,
                          const wxString &path, const std::string &content);

  // `path` as an absolute path of the remote host: a leading `~` or `$HOME` is
  // the remote home, and a relative path is under it. The path does not have
  // to exist. Blocking, like ReadFile.
  static StatusOr<wxString>
  ResolvePath(const wxString &host, const wxString &user, const wxString &path);

  // Whether `path` exists on the remote host (a file or a folder). `~/` and
  // `$HOME/` expand to the remote home. Blocking, like ReadFile.
  static StatusOr<bool> Exists(const wxString &host, const wxString &user,
                               const wxString &path);

  // Deletes `path` and everything in it (a symbolic link is removed, never
  // followed). A path that does not exist is fine. Afterwards each of
  // `emptyParents` is removed if it is empty, and left alone if not. `~/` and
  // `$HOME/` expand to the remote home. Blocking, like ReadFile.
  //
  // This is as dangerous as it sounds: the caller must make sure that `path` is
  // a folder it created (ReviewLoop::IsReviewFolder() does that). The checks
  // here only stop the most obvious mistakes: an empty path, `/`, `~`.
  static Status RemoveTree(const wxString &host, const wxString &user,
                           const wxString &path,
                           const std::vector<wxString> &emptyParents = {});

  // Deletes the file `path` (a symbolic link is removed, never followed). A
  // folder is an error and is left as it is; a path that does not exist is
  // fine. `~/` and `$HOME/` expand to the remote home. Blocking, like ReadFile.
  static Status RemoveFile(const wxString &host, const wxString &user,
                           const wxString &path);

  // Creates the file at `path`, or replaces it, with `content`; missing parent
  // folders are created. `~/` and `$HOME/` expand to the remote home. Unlike
  // WriteFile this is not atomic, it is meant for small new files. With
  // `exclusive`, a path that exists (a file, a folder or a symbolic link, which
  // is not followed) is an error and is left as it is; a file that this call
  // created and could not write (or close) is removed again. Blocking.
  static Status PutFile(const wxString &host, const wxString &user,
                        const wxString &path, const std::string &content,
                        bool exclusive = false);
};
