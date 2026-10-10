#include "app/ReviewBuddy.hpp"

#include "MainFrame.h"
#include "core/Logger.h"
#include "core/SftpClient.h"

#include "terminal_view.h"

#include <wx/app.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/notifmsg.h>
#include <wx/toplevel.h>
#include <wx/utils.h>

#include <thread>

namespace {
constexpr int kLocalPollMs = 1000;
constexpr int kRemotePollMs = 4000;
// The delay between typing a line and pressing Enter: some TUIs take text and
// Enter that arrive together as one paste, and do not submit it.
constexpr int kEnterDelayMs = 400;
constexpr auto kStallTimeout = std::chrono::minutes(20);
constexpr int kMaxRemoteErrors = 5;

wxString JoinPath(const wxString &dir, const wxString &name) {
  return dir.EndsWith("/") || dir.EndsWith("\\") ? dir + name
                                                 : dir + "/" + name;
}
} // namespace

wxDEFINE_EVENT(wxEVT_REVIEW_CHANGED, wxCommandEvent);

ReviewBuddy::ReviewBuddy(const Target &target, wxTerminalViewCtrl *main,
                         LaunchFn launchReviewer, FocusFn focusTerminal)
    : m_target(target), m_remote(!target.remoteHost.empty()), m_main(main),
      m_launchReviewer(std::move(launchReviewer)),
      m_focusTerminal(std::move(focusTerminal)), m_pollTimer(this),
      m_enterTimer(this) {
  Bind(wxEVT_TIMER, &ReviewBuddy::OnPoll, this, m_pollTimer.GetId());
  Bind(wxEVT_TIMER, &ReviewBuddy::OnEnterTimer, this, m_enterTimer.GetId());
}

ReviewBuddy::~ReviewBuddy() {
  m_alive->store(false);
  m_pollTimer.Stop();
  m_enterTimer.Stop();
  // Windows cannot delete a folder that a running process uses as its working
  // directory: if the reviewer is not dead yet, the folder may stay (logged).
  if (!m_removeOnClose.empty()) {
    if (m_remote) {
      RemoveRemote(m_removeOnClose);
    } else {
      RemoveLocal(m_removeOnClose);
    }
  }
}

bool ReviewBuddy::IsRunning() const { return m_loop && m_loop->IsActive(); }

bool ReviewBuddy::HasStalled() const {
  return m_loop && m_loop->GetState() == ReviewLoop::State::Stalled;
}

bool ReviewBuddy::IsInProgress() const {
  if (!m_loop) {
    return !m_stopped; // Starting
  }
  switch (m_loop->GetState()) {
  case ReviewLoop::State::Idle:
  case ReviewLoop::State::Reviewing:
  case ReviewLoop::State::Fixing:
  case ReviewLoop::State::Stalled:
    return true;
  case ReviewLoop::State::Done:
  case ReviewLoop::State::Stopped:
    break;
  }
  return false;
}

wxString ReviewBuddy::Describe() const {
  return m_loop ? m_loop->Describe() : _("Starting");
}

void ReviewBuddy::NotifyChanged() {
  if (m_eventTarget == nullptr) {
    return;
  }
  wxCommandEvent event(wxEVT_REVIEW_CHANGED);
  event.SetEventObject(this);
  m_eventTarget->ProcessEvent(event);
}

wxString ReviewBuddy::StatusText() const {
  if (!m_loop) {
    return m_stopped ? _("Review stopped") : _("Starting review");
  }
  const wxString rounds =
      wxString::Format(_("round %d/%d"), m_loop->Round(), m_loop->MaxRounds());
  switch (m_loop->GetState()) {
  case ReviewLoop::State::Reviewing:
    return m_loop->IsSlow()
               ? wxString::Format(_("Waiting for review (%s - slow)"), rounds)
               : wxString::Format(_("Waiting for review (%s)"), rounds);
  case ReviewLoop::State::Fixing:
    return m_loop->IsSlow()
               ? wxString::Format(_("Addressing comments (%s - slow)"), rounds)
               : wxString::Format(_("Addressing comments (%s)"), rounds);
  case ReviewLoop::State::Stalled:
    return wxString::Format(_("Review stalled: %s"), m_loop->Message());
  case ReviewLoop::State::Stopped:
    return _("Review stopped");
  case ReviewLoop::State::Done:
    return wxString::Format(_("Review finished: %s"), m_loop->Message());
  case ReviewLoop::State::Idle:
    break;
  }
  return _("Starting review");
}

wxString ReviewBuddy::CommentsPath() const {
  return m_loop ? m_loop->CommentsPath() : wxString{};
}

void ReviewBuddy::Begin() {
  if (m_loop || m_stopped) {
    return;
  }
  m_loop = std::make_unique<ReviewLoop>(ReviewLoop::NewId());
  KLOG_INFO() << "Review buddy " << m_loop->Id() << " starts in '"
              << m_target.workingDir << "'" << (m_remote ? " (remote)" : "");
  AddIgnoreRule();
  m_pollTimer.Start(m_remote ? kRemotePollMs : kLocalPollMs);
  Execute(m_loop->Start());
}

void ReviewBuddy::Resend() {
  // Not while a file is being written or checked: its answer would run the
  // old actions a second time.
  if (m_loop && !m_ioBusy) {
    const bool wasStalled = HasStalled();
    Execute(m_loop->Resend());
    if (wasStalled && IsRunning()) {
      m_pollTimer.Start(m_remote ? kRemotePollMs : kLocalPollMs);
    }
  }
}

void ReviewBuddy::Stop() {
  m_stopped = true;
  if (m_loop) {
    m_loop->Stop();
  }
  m_pollTimer.Stop();
  m_enterTimer.Stop();
  m_enterTarget = nullptr;
  NotifyChanged();
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

void ReviewBuddy::OnPoll(wxTimerEvent &) {
  if (!IsRunning() || m_ioBusy) {
    return;
  }
  if (std::chrono::steady_clock::now() - m_lastProgress > kStallTimeout) {
    Execute(m_loop->OnSlow());
    return;
  }

  const wxString marker = m_loop->WatchedMarker();
  const wxString fileToRead = m_loop->FileToRead();
  if (m_remote) {
    CheckRemote(marker, fileToRead);
    return;
  }
  if (!wxFileName::FileExists(LocalPath(marker))) {
    return;
  }
  const wxString content =
      fileToRead.empty() ? wxString{} : ReadLocal(fileToRead);
  Execute(m_loop->OnMarkerFound(content));
}

void ReviewBuddy::CheckRemote(const wxString &marker,
                              const wxString &fileToRead) {
  m_ioBusy = true;
  const wxString host = m_target.remoteHost;
  const wxString user = m_target.remoteUser;
  const wxString markerPath = RemotePath(marker);
  const wxString filePath =
      fileToRead.empty() ? wxString{} : RemotePath(fileToRead);

  std::thread([this, alive = m_alive, host, user, markerPath, filePath] {
    auto exists = SftpClient::Exists(host, user, markerPath);
    bool found = exists.ok() && exists.value();
    wxString content;
    wxString error;
    if (!exists.ok()) {
      error = exists.status().message();
    } else if (found && !filePath.empty()) {
      auto file = SftpClient::ReadFile(host, user, filePath, {});
      if (file.ok()) {
        content = wxString::FromUTF8(file.value().content);
      }
    }
    CallAfterIfAlive(alive, [this, found, content, error] {
      m_ioBusy = false;
      if (!IsRunning()) {
        return;
      }
      if (!error.empty()) {
        KLOG_WARN() << "Review buddy: remote check failed: " << error;
        if (++m_remoteErrors >= kMaxRemoteErrors) {
          Execute(m_loop->Fail(_("Cannot reach the remote host: ") + error));
        }
        return;
      }
      m_remoteErrors = 0;
      if (found) {
        Execute(m_loop->OnMarkerFound(content));
      }
    });
  }).detach();
}

// ---------------------------------------------------------------------------
// Doing what the loop asks for
// ---------------------------------------------------------------------------

void ReviewBuddy::Execute(Actions actions, size_t from) {
  m_lastProgress = std::chrono::steady_clock::now();
  // The loop changed its state before it handed over these actions.
  NotifyChanged();
  for (size_t i = from; i < actions.size(); ++i) {
    const auto &action = actions[i];
    switch (action.kind) {
    case ReviewLoop::Action::Kind::WriteFile:
      if (m_remote) {
        WriteRemote(action.path, action.text, std::move(actions), i + 1);
        return;
      }
      if (!WriteLocal(action.path, action.text)) {
        Execute(m_loop->Fail(_("Could not write ") + action.path));
        return;
      }
      break;
    case ReviewLoop::Action::Kind::PasteToReviewer:
      if (m_reviewer == nullptr) {
        // The first request: the reviewer starts with it as its first message,
        // so there is nothing to wait for. The request file is written already
        // (the actions run in order).
        m_reviewer = m_launchReviewer
                         ? m_launchReviewer(action.text, m_loop->Folder())
                         : nullptr;
        if (m_reviewer == nullptr) {
          Execute(m_loop->Fail(_("Could not start the reviewer")));
          return;
        }
      } else {
        PasteLine(m_reviewer, action.text);
      }
      break;
    case ReviewLoop::Action::Kind::PasteToMain:
      PasteLine(m_main, action.text);
      break;
    case ReviewLoop::Action::Kind::RemoveFolder:
      if (!ReviewLoop::IsReviewFolder(action.path)) {
        KLOG_ERROR() << "Review buddy: not removing '" << action.path << "'";
        break;
      }
      // The reviewer runs in this folder: it goes when its pane closes.
      m_removeOnClose = action.path;
      break;
    case ReviewLoop::Action::Kind::Notify:
      KLOG_INFO() << "Review buddy: " << action.text;
      NotifyUser(_("Review needs your attention"), action.text);
      break;
    case ReviewLoop::Action::Kind::Finished:
      Finished();
      break;
    }
  }
  KLOG_INFO() << "Review buddy: " << m_loop->Describe();
}

void ReviewBuddy::WriteRemote(const wxString &relPath, const wxString &text,
                              Actions rest, size_t next) {
  m_ioBusy = true;
  const wxString host = m_target.remoteHost;
  const wxString user = m_target.remoteUser;
  const wxString path = RemotePath(relPath);
  const std::string content = text.ToStdString(wxConvUTF8);

  std::thread([this, alive = m_alive, host, user, path, content,
               rest = std::move(rest), next]() mutable {
    Status st = SftpClient::PutFile(host, user, path, content);
    CallAfterIfAlive(alive,
                     [this, st, path, rest = std::move(rest), next]() mutable {
                       m_ioBusy = false;
                       if (!m_loop) {
                         return;
                       }
                       if (!st.ok()) {
                         // Does nothing if the loop was stopped meanwhile.
                         Execute(m_loop->Fail(_("Could not write ") + path +
                                              ": " + st.message()));
                         return;
                       }
                       if (!m_loop->IsActive()) {
                         return; // Stopped while the file was being written
                       }
                       Execute(std::move(rest), next);
                     });
  }).detach();
}

void ReviewBuddy::RemoveRemote(const wxString &relPath) {
  const wxString host = m_target.remoteHost;
  const wxString user = m_target.remoteUser;
  const wxString path = RemotePath(relPath);
  const std::vector<wxString> parents = {RemotePath(".agents/reviews"),
                                         RemotePath(".agents")};

  // Not tied to this object: it is going away. If Kennel exits before the
  // thread is done, the remote folder stays.
  std::thread([host, user, path, parents]() {
    Status st = SftpClient::RemoveTree(host, user, path, parents);
    if (!st.ok()) {
      KLOG_WARN() << "Review buddy: could not remove " << path << ": "
                  << st.message();
    }
  }).detach();
}

bool ReviewBuddy::RemoveLocal(const wxString &relPath) {
  const wxString folder = LocalPath(relPath);
  // A symbolic link in its place is not ours: leave it.
  if (wxFileName::Exists(folder,
                         wxFILE_EXISTS_SYMLINK | wxFILE_EXISTS_NO_FOLLOW)) {
    KLOG_WARN() << "Review buddy: '" << folder << "' is a symbolic link";
    return false;
  }
  if (wxDir::Exists(folder) &&
      !wxFileName::Rmdir(folder, wxPATH_RMDIR_RECURSIVE)) {
    KLOG_WARN() << "Review buddy: could not remove '" << folder << "'";
    return false;
  }
  // Leave no trace: the folders around it, if nothing else is in them.
  for (const wxString &parent :
       {LocalPath(".agents/reviews"), LocalPath(".agents")}) {
    wxDir dir(parent);
    if (dir.IsOpened() && !dir.HasFiles() && !dir.HasSubDirs()) {
      dir.Close();
      wxFileName::Rmdir(parent);
    }
  }
  return true;
}

void ReviewBuddy::PasteLine(wxTerminalViewCtrl *terminal,
                            const wxString &line) {
  if (terminal == nullptr) {
    return;
  }
  terminal->SendInput(line.ToStdString(wxConvUTF8));
  m_enterTarget = terminal;
  m_enterTimer.StartOnce(kEnterDelayMs);
  // This agent is the active one now.
  if (m_focusTerminal) {
    m_focusTerminal(terminal);
  }
}

void ReviewBuddy::OnEnterTimer(wxTimerEvent &) {
  if (m_enterTarget != nullptr) {
    m_enterTarget->SendEnter();
    m_enterTarget = nullptr;
  }
}

void ReviewBuddy::Finished() {
  m_pollTimer.Stop();
  KLOG_INFO() << "Review buddy ended: " << m_loop->Message();
  const bool done = m_loop->GetState() == ReviewLoop::State::Done;
  NotifyUser(done ? _("Review finished") : _("Review needs your attention"),
             m_loop->Message());
}

void ReviewBuddy::NotifyUser(const wxString &title, const wxString &message) {
  if (wxTheApp == nullptr) {
    return;
  }
  const wxString text = m_target.sessionName.empty()
                            ? message
                            : m_target.sessionName + ": " + message;

  // Always shown, even when the user is looking at this session: the end of a
  // review is easy to miss in the status bar.
#ifdef __WXOSX__
  // The system notification does not show up reliably here: a dialog does. It
  // runs after the current handler, which may be in the middle of the actions
  // of the loop (a dialog runs its own event loop).
  wxTheApp->CallAfter([title, text]() {
    wxMessageBox(text, title, wxOK | wxICON_INFORMATION,
                 wxTheApp->GetTopWindow());
  });
#else
  wxNotificationMessage notification(title, text, wxTheApp->GetTopWindow());
  notification.Show();
#endif

  // Kennel is in the background: the Dock icon bounces on macOS, the taskbar
  // button flashes on Windows.
  if (!wxTheApp->IsActive()) {
    if (auto *frame =
            dynamic_cast<wxTopLevelWindow *>(wxTheApp->GetTopWindow())) {
      frame->RequestUserAttention(wxUSER_ATTENTION_INFO);
    }
  }
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

wxString ReviewBuddy::LocalPath(const wxString &relPath) const {
  return JoinPath(m_target.workingDir, relPath);
}

wxString ReviewBuddy::RemotePath(const wxString &relPath) const {
  return JoinPath(m_target.workingDir.empty() ? wxString("~")
                                              : m_target.workingDir,
                  relPath);
}

bool ReviewBuddy::WriteLocal(const wxString &relPath, const wxString &text) {
  wxFileName fn(LocalPath(relPath));
  // Mkdir() of a wxFileName makes its folder part (GetPath()), not the file.
  if (!fn.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    return false;
  }
  wxFFile file(fn.GetFullPath(), "wb");
  return file.IsOpened() && file.Write(text, wxConvUTF8);
}

wxString ReviewBuddy::ReadLocal(const wxString &relPath) const {
  wxFFile file(LocalPath(relPath), "rb");
  wxString content;
  if (file.IsOpened()) {
    file.ReadAll(&content, wxConvUTF8);
  }
  return content;
}

void ReviewBuddy::AddIgnoreRule() const {
  // Keep our files out of `git status`, without touching the repository's own
  // .gitignore. Best effort, and only for a plain .git folder (a worktree has a
  // .git file instead). Over SSH the review request asks the agent to ignore
  // the folder.
  if (m_remote) {
    return;
  }
  const wxString exclude = LocalPath(".git/info/exclude");
  if (!wxDir::Exists(LocalPath(".git"))) {
    return;
  }
  wxString content;
  if (wxFileName::FileExists(exclude)) {
    wxFFile in(exclude, "rb");
    if (in.IsOpened()) {
      in.ReadAll(&content, wxConvUTF8);
    }
  }
  if (content.Contains(".agents/reviews/")) {
    return;
  }
  wxFileName::Mkdir(LocalPath(".git/info"), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  wxFFile out(exclude, "ab");
  if (out.IsOpened()) {
    out.Write(wxString(content.empty() || content.EndsWith("\n") ? "" : "\n") +
                  ".agents/reviews/\n",
              wxConvUTF8);
  }
}
