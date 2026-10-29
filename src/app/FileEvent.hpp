#pragma once

#include "core/Account.h"
#include "core/Status.h"

#include <wx/event.h>
#include <wx/string.h>

#include <string>

// Where a remote file lives. Empty (no host) means "not remote". The same
// information as an Account of the kind HostType::kRemote.
struct RemoteHostDetails {
  wxString host; // Host name, or an ~/.ssh/config alias
  wxString user; // Empty -> the ssh default
};

// Event about a file or a folder: which one (the path, and the Account it is
// on), plus the usual wxCommandEvent payload (GetInt()/GetString()). Sent by
// the file views (FileTreeCtrl, see its events) and by FilePage (the save
// events at the end of this file).
class FileEvent : public wxCommandEvent {
public:
  explicit FileEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  FileEvent(const FileEvent &) = default;

  wxEvent *Clone() const override { return new FileEvent(*this); }

  // The full path, in the notation of the file system it is on (a Linux path
  // for a remote host or for WSL).
  const wxString &GetPath() const { return m_path; }
  void SetPath(const wxString &path) { m_path = path; }

  // Whether the path is a folder.
  bool IsDir() const { return m_isDir; }
  void SetIsDir(bool isDir) { m_isDir = isDir; }

  // The place the file is on.
  const Account &GetAccount() const { return m_account; }
  void SetAccount(const Account &account) { m_account = account; }

  bool IsRemote() const { return m_account.IsRemote(); }
  // The remote host (and user) of the file, empty for a file that is not on a
  // remote account. Kept for the code that works with RemoteHostDetails; the
  // account holds it.
  RemoteHostDetails GetRemoteHostDetails() const {
    if (!m_account.IsRemote()) {
      return {};
    }
    return {m_account.m_name, m_account.m_user};
  }
  void SetRemoteHostDetails(const RemoteHostDetails &details) {
    m_account = details.host.empty()
                    ? Account::LocalHost()
                    : Account::Remote(details.host, details.user);
  }

  // Outcome of the operation the event reports (e.g. a save).
  const Status &GetStatusCode() const { return m_status; }
  void SetStatusCode(const Status &status) { m_status = status; }

  // Identifies the file across opens: "<path>" locally, "<host>:<path>"
  // remotely.
  wxString GetKey() const { return MakeKey(m_path, GetRemoteHostDetails()); }
  static wxString MakeKey(const wxString &path,
                          const RemoteHostDetails &remoteHost) {
    return remoteHost.host.empty() ? path : remoteHost.host + ":" + path;
  }

private:
  wxString m_path;
  bool m_isDir{false};
  Account m_account;
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
