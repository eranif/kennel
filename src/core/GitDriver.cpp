#include "core/GitDriver.h"

#include "core/EventNotifier.hpp"
#include "core/GitEvent.h"
#include "core/GitParser.h"

namespace {
wxString Utf8(const std::string &text) {
  return wxString::FromUTF8(text.data(), text.size());
}

// What git wrote when it failed.
wxString ErrorText(const ProcessOutput &output) {
  const wxString text = Utf8(output.err.empty() ? output.out : output.err);
  return text.empty() ? wxString(_("git failed")) : text.Strip(wxString::both);
}

// The event of an answer: it carries the error when `output` is one.
GitEvent MakeEvent(wxEventType type, GitDriver *driver,
                   const ProcessOutput &output) {
  GitEvent event(type);
  event.SetEventObject(driver);
  if (!output.ok) {
    event.SetResult(::Status::Error(ErrorText(output)));
  }
  return event;
}
} // namespace

GitDriver::GitDriver(const wxString &folder) : m_folder(folder) {
  m_worker = std::thread([this] { WorkerLoop(); });
}

GitDriver::~GitDriver() { Shutdown(); }

void GitDriver::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear(); // Not started jobs are dropped
    m_stop = true;
  }
  m_cv.notify_all();
  if (m_worker.joinable()) {
    m_worker.join();
  }
}

void GitDriver::AddJob(Job job) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(std::move(job));
  }
  m_cv.notify_one();
}

void GitDriver::WorkerLoop() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
      if (m_queue.empty()) {
        return; // Stopped, and nothing is left to run
      }
      job = std::move(m_queue.front());
      m_queue.pop_front();
    }
    job.cb();
  }
}

void GitDriver::Status() {
  AddJob({[this] {
    const ProcessOutput output =
        RunGit({"--no-optional-locks", "status", "--porcelain=v2", "-b", "-z",
                "--untracked-files=all"});
    wxString root;
    if (output.ok) {
      const ProcessOutput top = RunGit({"rev-parse", "--show-toplevel"});
      root = Utf8(top.out).Strip(wxString::both);
    }
    CallAfter(&GitDriver::OnStatusOutput, output, root);
  }});
}

void GitDriver::GetCurrentBranchName() {
  AddJob({[this] {
    const ProcessOutput output =
        RunGit({"symbolic-ref", "--short", "-q", "HEAD"});
    CallAfter(&GitDriver::OnBranchNameOutput, output);
  }});
}

void GitDriver::Diff(const wxString &path, bool staged) {
  AddJob({[this, path, staged] {
    // ":(top,literal)": the path is relative to the root of the repository
    // (as in the status) and it is a name, not a pattern.
    std::vector<std::string> args = {"diff", "--no-color"};
    if (staged) {
      args.push_back("--cached");
    }
    args.push_back("--");
    args.push_back(":(top,literal)" + path.ToStdString(wxConvUTF8));
    const ProcessOutput output = RunGit(args);
    CallAfter(&GitDriver::OnDiffOutput, path, output);
  }});
}

void GitDriver::OnStatusOutput(const ProcessOutput &output,
                               const wxString &root) {
  GitEvent event = MakeEvent(wxEVT_GIT_STATUS, this, output);
  if (output.ok) {
    GitStatus status = git::ParseStatus(output.out);
    status.root = root;
    event.SetRepoStatus(status);
  }
  EventNotifier::Get()->AddPendingEvent(event);
}

void GitDriver::OnBranchNameOutput(const ProcessOutput &output) {
  // `-q`: a detached HEAD is not an error, it gives no output (exit code 1)
  ProcessOutput fixed = output;
  if (!fixed.ok && fixed.err.empty()) {
    fixed.ok = true;
  }
  GitEvent event = MakeEvent(wxEVT_GIT_BRANCH_NAME, this, fixed);
  if (fixed.ok) {
    event.SetText(Utf8(fixed.out).Strip(wxString::both));
  }
  EventNotifier::Get()->AddPendingEvent(event);
}

void GitDriver::OnDiffOutput(const wxString &path,
                             const ProcessOutput &output) {
  GitEvent event = MakeEvent(wxEVT_GIT_DIFF, this, output);
  event.SetPath(path);
  if (output.ok) {
    event.SetText(Utf8(output.out));
  }
  EventNotifier::Get()->AddPendingEvent(event);
}
