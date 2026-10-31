#pragma once

#include "core/Process.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <wx/event.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/string.h>

// Runs git for one folder. The subclasses say where: GitDriverLocal (this
// machine), GitDriverWSL and GitDriverSSH. GUI-free.
//
// The driver has a worker thread with a queue of jobs. A request (Status(),
// Diff(), ...) only adds a job and returns, so the UI is never blocked. The job
// runs git on the worker thread, then hands the output to the UI thread with
// CallAfter(), where it becomes a GitEvent (see core/GitEvent.h) on the
// EventNotifier, with the id of the driver in GetInt().
//
// A subclass must call Shutdown() first in its destructor: the worker calls
// RunGit(), which must not run when the subclass is gone.
class GitDriver : public wxEvtHandler {
public:
  ~GitDriver() override;

  // The answer is wxEVT_GIT_STATUS.
  void Status();
  // The answer is wxEVT_GIT_BRANCH_NAME. The text is empty when HEAD is
  // detached.
  void GetCurrentBranchName();
  // `path` is a path of GitChange. The answer is wxEVT_GIT_DIFF.
  void Diff(const wxString &path, bool staged);

  const wxString &GetFolder() const { return m_folder; }
  // Tells the drivers apart: it is in the events (GetInt()). Never reused.
  int GetId() const { return m_id; }

protected:
  explicit GitDriver(const wxString &folder);

  // Runs `git <args>` in the folder and waits for it. Called on the worker
  // thread only.
  virtual ProcessOutput RunGit(const std::vector<std::string> &args) = 0;
  // How the paths of the folder are written: UNIX, unless it is a folder of
  // this machine.
  virtual wxPathFormat GetPathFormat() const { return wxPATH_UNIX; }

  // Runs a program and waits for it. On Windows the command line is built from
  // `argv` with the quoting that programs expect (Process joins the elements
  // with a space).
  static ProcessOutput RunProcess(const std::vector<std::string> &argv);

  // Drops the jobs that did not start, waits for the one that runs, and joins
  // the worker. Safe to call more than once.
  void Shutdown();

private:
  struct Job {
    std::function<void()> cb;
  };

  void AddJob(Job job);
  void WorkerLoop();

  // On the UI thread.
  void OnStatusOutput(const ProcessOutput &output, const wxString &root);
  void OnBranchNameOutput(const ProcessOutput &output);
  void OnDiffOutput(const wxString &path, const ProcessOutput &output);

  int m_id;
  // A Status() job waits in the queue: another request needs no new job.
  std::atomic<bool> m_statusQueued{false};
  wxString m_folder;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::deque<Job> m_queue;
  bool m_stop{false};
  std::thread m_worker;
};
