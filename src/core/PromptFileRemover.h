#pragma once

#include "core/Workspace.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

// Deletes the file of the first message of an agent (a PromptFile, see
// Session::promptFile) when nobody else will: a local file at once, a remote
// one on a worker thread (SFTP can take seconds). Only a file or a symbolic
// link is deleted, never a folder (SftpClient::RemoveFile()): a folder that
// took the place of the file is left alone.
//
// KennelApp waits for the remote ones at exit (Flush()), so that Kennel does
// not end in the middle of one. GUI-free; any thread.
class PromptFileRemover {
public:
  static PromptFileRemover &Get();

  // Deletes `file`; nothing when it is not set or is gone already.
  void Remove(const PromptFile &file);
  // A worker that may still give a file to Remove() (a remote write that runs,
  // HandoffDelivery): Flush() waits for it too. BeginJob() before its thread
  // starts, EndJob() when it is done (after its last Remove()).
  void BeginJob();
  void EndJob();
  // Waits until the remote deletes and the jobs that run are done, at most
  // `limit`.
  void Flush(std::chrono::milliseconds limit);

private:
  PromptFileRemover() = default;
  void RemoveLocal(const wxString &path) const;

  std::mutex m_mutex;
  std::condition_variable m_done;
  // The remote deletes and the jobs that run.
  int m_pending{0};
};
