#include "core/PromptFileRemover.h"

#include "core/Logger.h"
#include "core/SftpClient.h"

#include <wx/filefn.h>
#include <wx/filename.h>

#include <thread>

PromptFileRemover &PromptFileRemover::Get() {
  // Never freed: a delete that runs at exit may still use it after Flush()
  // gave up waiting.
  static auto *remover = new PromptFileRemover;
  return *remover;
}

void PromptFileRemover::Remove(const PromptFile &file) {
  if (!file.IsSet()) {
    return;
  }
  if (file.remoteHost.empty()) {
    RemoveLocal(file.path);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_pending;
  }
  std::thread([this, file] {
    const Status st =
        SftpClient::RemoveFile(file.remoteHost, file.remoteUser, file.path);
    if (!st.ok()) {
      KLOG_WARN() << "Could not remove '" << file.path << "' on "
                  << file.remoteHost << ": " << st.message();
    }
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      --m_pending;
    }
    m_done.notify_all();
  }).detach();
}

void PromptFileRemover::BeginJob() {
  std::lock_guard<std::mutex> lock(m_mutex);
  ++m_pending;
}

void PromptFileRemover::EndJob() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    --m_pending;
  }
  m_done.notify_all();
}

void PromptFileRemover::RemoveLocal(const wxString &path) const {
  // A file, or a symbolic link (not followed: the link goes, not its target).
  // A folder is left alone, and so is a path that is gone.
  if (!wxFileName::Exists(path, wxFILE_EXISTS_REGULAR | wxFILE_EXISTS_SYMLINK |
                                    wxFILE_EXISTS_NO_FOLLOW)) {
    return;
  }
  if (!wxRemoveFile(path)) {
    KLOG_WARN() << "Could not remove '" << path << "'";
  }
}

void PromptFileRemover::Flush(std::chrono::milliseconds limit) {
  std::unique_lock<std::mutex> lock(m_mutex);
  if (!m_done.wait_for(lock, limit, [this] { return m_pending == 0; })) {
    KLOG_WARN() << m_pending
                << " remote file delete(s) or write(s) not done before exit";
  }
}
