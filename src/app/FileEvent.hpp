#pragma once

#include "core/Status.h"

#include <wx/event.h>
#include <wx/string.h>

#include <string>

// Where a remote file lives. Empty (no host) means "not remote".
struct RemoteHostDetails {
  wxString host; // Host name, or an ~/.ssh/config alias
  wxString user; // Empty -> the ssh default
};

// Event about a file shown in the main view: which file (path + where it
// lives), plus the usual wxCommandEvent payload (GetInt()/GetString()).
class FileEvent : public wxCommandEvent {
public:
  explicit FileEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  FileEvent(const FileEvent &) = default;

  wxEvent *Clone() const override { return new FileEvent(*this); }

  const wxString &GetFilePath() const { return m_path; }
  void SetFilePath(const wxString &path) { m_path = path; }

  bool IsRemote() const { return !m_remoteHost.host.empty(); }
  const RemoteHostDetails &GetRemoteHostDetails() const { return m_remoteHost; }
  void SetRemoteHostDetails(const RemoteHostDetails &details) {
    m_remoteHost = details;
  }

  // Outcome of the operation the event reports (e.g. a save).
  const Status &GetStatusCode() const { return m_status; }
  void SetStatusCode(const Status &status) { m_status = status; }

  // Identifies the file across opens: "<path>" locally, "<host>:<path>"
  // remotely.
  wxString GetKey() const { return MakeKey(m_path, m_remoteHost); }
  static wxString MakeKey(const wxString &path,
                          const RemoteHostDetails &remoteHost) {
    return remoteHost.host.empty() ? path : remoteHost.host + ":" + path;
  }

private:
  wxString m_path;
  RemoteHostDetails m_remoteHost;
  Status m_status;
};

// Sent (processed, on the UI thread) by a FilePage when SaveAsync() starts an
// upload.
wxDECLARE_EVENT(wxEVT_FILE_SAVE_STARTED, FileEvent);
// Sent (queued) by the FilePage's save thread to the page itself when the
// upload finishes; GetStatusCode() holds the outcome. The page handles it first
// (the file's own state) and Skip()s it, so it propagates up to wxTheApp, where
// MainView handles the rest.
wxDECLARE_EVENT(wxEVT_FILE_SAVE_DONE, FileEvent);

// Outcome of a background SFTP read, carried by wxEVT_REMOTE_FILE_READ.
struct RemoteReadResult {
  bool ok{false};
  wxString error;               // Set when !ok
  RemoteHostDetails remoteHost; // Where it was read from...
  wxString clickedPath; // ...and the path as it appeared in the terminal
  wxString path;        // The resolved absolute path; set when ok
  std::string content;  // The file's bytes; set when ok
};

// Posted to wxTheApp from a worker thread when an SFTP read finishes; the
// payload is a RemoteReadResult. MainView handles it on the UI thread.
wxDECLARE_EVENT(wxEVT_REMOTE_FILE_READ, wxThreadEvent);
