#pragma once

#include "core/Account.h"
#include "core/Status.h"

#include <atomic>
#include <future>
#include <vector>
#include <wx/event.h>
#include <wx/string.h>

// What to look for, and where.
struct FindQuery {
  wxString pattern;
  bool isRegex{false};
  bool matchCase{false};
  bool wholeWord{false};
  // Globs separated by commas, e.g. "*.cpp, *.hpp". Empty: every file. A file
  // must match one of the `include` globs and none of the `exclude` globs.
  wxString include;
  wxString exclude;
  // The folder to search, in the notation of the account (a Linux path for a
  // remote host and for WSL).
  wxString root;
};

// One line that has a match.
struct FindMatch {
  // The file, in the notation of the account: `root` joined with the path of
  // the file in it.
  wxString path;
  int line{0};   // Starts at 1
  int column{0}; // Of the first match in the line, starts at 1
  // The line, without the indentation and the end of the line, and cut when it
  // is very long.
  wxString text;
  // The first match in `text`, in characters.
  int matchStart{0};
  int matchLength{0};
};

// The answers of FindInFiles. Sent to the handler given to FindInFiles, on its
// thread (the UI thread).
class FindEvent : public wxCommandEvent {
public:
  explicit FindEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  FindEvent(const FindEvent &) = default;
  wxEvent *Clone() const override { return new FindEvent(*this); }

  // wxEVT_FIND_BATCH: the matches that were found since the last batch.
  const std::vector<FindMatch> &GetMatches() const { return m_matches; }
  std::vector<FindMatch> &Matches() { return m_matches; }

  // wxEVT_FIND_DONE: how it ended. An error status when the search could not
  // run or ripgrep failed; the matches that came before are still valid.
  const Status &GetResult() const { return m_result; }
  void SetResult(const Status &result) { m_result = result; }
  // The search stopped at the limit of matches (FindInFiles::kMaxMatches).
  bool IsTruncated() const { return m_truncated; }
  void SetTruncated(bool truncated) { m_truncated = truncated; }
  // The search was stopped by Cancel().
  bool IsCancelled() const { return m_cancelled; }
  void SetCancelled(bool cancelled) { m_cancelled = cancelled; }

private:
  std::vector<FindMatch> m_matches;
  Status m_result;
  bool m_truncated{false};
  bool m_cancelled{false};
};

// Both events carry the number of their search in GetInt(): the
// FindInFiles::GetGeneration() of the Start() that began it. A search that
// ended can still have events waiting in the queue of the handler when the next
// one starts, so a handler that starts searches one after the other ignores the
// ones that are not of the search it started last.
// Sent while the search runs, any number of times, in order.
wxDECLARE_EVENT(wxEVT_FIND_BATCH, FindEvent);
// Sent once, last, whatever the outcome.
wxDECLARE_EVENT(wxEVT_FIND_DONE, FindEvent);

// Searches the files of an account with ripgrep (see Ripgrep.h) on a thread of
// its own and reports with FindEvent. One search at a time.
//
// A search of this machine or of a WSL distribution streams: the matches come
// while rg runs, and Cancel() stops it within a few milliseconds. A remote
// search runs the command over ssh and reports when it ends (SshClient does not
// stream), so there Cancel() only drops the result.
//
// The handler must outlive this object. The destructor cancels and waits for
// the worker, so destroying a FindInFiles during a remote search blocks until
// the command ends (up to its timeout of 5 minutes): SshClient::Execute can not
// be interrupted.
class FindInFiles {
public:
  // Stop after this many matches.
  static constexpr size_t kMaxMatches = 5000;

  explicit FindInFiles(wxEvtHandler *handler);
  ~FindInFiles();

  FindInFiles(const FindInFiles &) = delete;
  FindInFiles &operator=(const FindInFiles &) = delete;

  // Starts the search. An error (and no event) when one is already running, or
  // the query has no pattern or no folder. Otherwise a wxEVT_FIND_DONE always
  // comes, also when rg is not installed (that is an error in its result).
  //
  // May be called from the handler of wxEVT_FIND_DONE (to search again): the
  // search that sent it is over by then.
  Status Start(const Account &account, const FindQuery &query);

  // Asks the running search to stop. wxEVT_FIND_DONE still comes. Does nothing
  // when none runs.
  void Cancel();

  bool IsRunning() const;

  // The number of the last search that Start() began (1 for the first). Only
  // the UI thread calls Start() and this.
  int GetGeneration() const { return m_generation; }

  // The arguments for rg for `query`, without the program and the path.
  static std::vector<std::string> BuildArguments(const FindQuery &query);

private:
  void Run(Account account, FindQuery query, int generation);

  wxEvtHandler *m_handler;
  std::atomic<bool> m_cancel{false};
  // The worker has queued wxEVT_FIND_DONE. The future can not say it: it is
  // ready only when the worker returned, a moment later.
  std::atomic<bool> m_reported{false};
  int m_generation{0};
  std::future<void> m_future;
};
