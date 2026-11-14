#pragma once

#include "core/Account.h"
#include "core/Status.h"
#include "core/ToolDescription.h"

#include <memory>
#include <wx/event.h>
#include <wx/string.h>

// The answers of RemoteInstaller. Sent to the handler given to RemoteInstaller,
// on its thread (the UI thread).
class InstallEvent : public wxCommandEvent {
public:
  explicit InstallEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  InstallEvent(const InstallEvent &) = default;
  wxEvent *Clone() const override { return new InstallEvent(*this); }

  // wxEVT_INSTALL_DONE: how it ended. An error status when nothing was
  // installed; the machine is as it was then (a half written file never has the
  // name of the program).
  const Status &GetResult() const { return m_result; }
  void SetResult(const Status &result) { m_result = result; }
  // The right version was there already, so nothing was downloaded.
  bool WasAlreadyInstalled() const { return m_alreadyInstalled; }
  void SetAlreadyInstalled(bool installed) { m_alreadyInstalled = installed; }
  // Stopped by Cancel().
  bool IsCancelled() const { return m_cancelled; }
  void SetCancelled(bool cancelled) { m_cancelled = cancelled; }
  // Where the program is: the target folder (without the slashes at its end)
  // plus the name of the program ("$HOME/.kennel/bin/rg"). Empty on an error.
  const wxString &GetPath() const { return m_path; }
  void SetPath(const wxString &path) { m_path = path; }

private:
  Status m_result;
  bool m_alreadyInstalled{false};
  bool m_cancelled{false};
  wxString m_path;
};

// Sent while it runs, any number of times, in order. GetString() says what is
// happening now: "Downloading ripgrep 15.2.0".
wxDECLARE_EVENT(wxEVT_INSTALL_PROGRESS, InstallEvent);
// Sent once, last, whatever the outcome (see InstallEvent for what it holds).
//
// IsRunning() turns false when the end is decided, a moment before this event
// reaches the handler: the UI thread can run another event in between. So the
// handler of this event is the place to start the next install, and a caller
// that waits for it should not call Start() from anywhere else meanwhile: the
// event of the old install and the one of the new could not be told apart.
wxDECLARE_EVENT(wxEVT_INSTALL_DONE, InstallEvent);

// Puts one tool (see ToolDescription.h) into a folder of an account: a remote
// host, a WSL distribution, or this machine when it runs Linux. It knows
// nothing about the tool, so it is for any tool and any folder.
//
// What it does:
//  1. Asks the machine what it is (`uname -s`, `uname -m`) and picks the asset
//     of the tool for it. No asset is an error that says which machine.
//  2. Runs `<folder>/<binary> --version`: when it starts with the banner of the
//     tool, the right version is installed and it is done.
//  3. Downloads the asset here, in this process, once (so the machine needs no
//     internet), and checks its SHA-256 against the pinned one: a download that
//     does not match is thrown away and nothing is written.
//  4. Takes the program out of the archive when the asset is one.
//  5. Writes it next to its final name and renames it, so that a failed upload
//     never leaves a half file that looks installed, and makes it executable.
//  6. Runs `--version` again and compares it with the banner.
//
// The folder is a path on the machine as a POSIX shell writes it: absolute, or
// starting with `~` or `$HOME` ("$HOME/.kennel/bin"). Missing folders are
// created.
//
// This machine on Windows is not served: Kennel ships its own tools there (see
// Ripgrep.h), and an install there is reported as an error. Same for macOS,
// as far as a tool has no asset for it.
//
// Nothing here blocks the UI thread, in any call. Everything that blocks (ssh,
// the wsl.exe calls, the file writes) runs on a thread of its own; the download
// is a wxWebRequest, started from the UI thread, and the event of its end
// starts the second thread. The work belongs to a job that both threads share,
// not to this object, so this object can be destroyed at any time: the
// destructor drops the connection to the handler and returns at once. A thread
// that is in the middle of an ssh call (SshClient can not be interrupted)
// finishes in the background and its events are dropped.
//
// One install at a time per object. Only the UI thread may call the methods.
// The handler must outlive this object.
class RemoteInstaller {
public:
  // A download is stopped when it goes over this many bytes (it is read in
  // pieces as it arrives, so it is never held in memory beyond the limit by
  // much), and so is one that says in its headers that it is bigger.
  static constexpr size_t kMaxDownload = 64 * 1024 * 1024;

  explicit RemoteInstaller(wxEvtHandler *handler);
  // Detaches from a running install (it is cancelled, no event comes). Never
  // waits.
  ~RemoteInstaller();

  RemoteInstaller(const RemoteInstaller &) = delete;
  RemoteInstaller &operator=(const RemoteInstaller &) = delete;

  // Starts. An error (and no event) when one install is running, `tool` has no
  // binary name, or `targetDir` is empty or is not absolute and does not start
  // with `~` or `$HOME` (a relative folder would be another one for ssh than
  // for WSL). Otherwise a wxEVT_INSTALL_DONE always comes (unless this object
  // is destroyed first, or Stop() was called). May be called from the handler
  // of wxEVT_INSTALL_DONE (to install the next tool).
  Status Start(const Account &account, const ToolDescription &tool,
               const wxString &targetDir);

  // Asks the running install to stop: the download is cancelled at once, a
  // step that runs (ssh, an upload) is not interrupted. wxEVT_INSTALL_DONE
  // still comes. Does nothing when none runs.
  void Cancel();

  bool IsRunning() const;

  // The application is going away: from now on no installer sends an event or
  // queues a call, whatever is still running. Threads that are in an ssh call
  // are not waited for (they end with the process), and the EventNotifier and
  // the handlers may be destroyed under them, so they must not be used then.
  // Called where the application starts to close (~MainFrame), like
  // AgentScan::Stop().
  static void Stop();

  // Whether the output of `<binary> --version` says the version of `banner`:
  // its first line is the banner, or starts with the banner and a space
  // ("ripgrep 15.2.0 (rev abc)" for "ripgrep 15.2.0").
  static bool MatchesBanner(const wxString &output, const wxString &banner);

private:
  class Job;

  wxEvtHandler *m_handler;
  std::shared_ptr<Job> m_job;
};
