#pragma once

#include "core/Process.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <wx/event.h>
#include <wx/intl.h>
#include <wx/string.h>

// Runs git for one folder. The subclasses say where: GitDriverLocal (this
// machine), GitDriverWSL and GitDriverSSH. GUI-free.
//
// The driver has a worker thread with a queue of jobs. A request (Status(),
// Diff(), ...) only adds a job and returns, so the UI is never blocked. The job
// runs git on the worker thread, then hands the output to the UI thread with
// CallAfter(), where it becomes a GitEvent (see core/GitEvent.h) on the
// EventNotifier. The event object is the driver.
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

protected:
  explicit GitDriver(const wxString &folder);

  // Runs `git <args>` in the folder and waits for it. Called on the worker
  // thread only.
  virtual ProcessOutput RunGit(const std::vector<std::string> &args) = 0;

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

  wxString m_folder;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::deque<Job> m_queue;
  bool m_stop{false};
  std::thread m_worker;
};
