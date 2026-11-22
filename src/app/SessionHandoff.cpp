#include "app/SessionHandoff.hpp"

#include "core/Logger.h"
#include "core/PromptFileRemover.h"
#include "core/SftpClient.h"
#include "core/WslPath.h"
#include "terminal_view.h"

#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>

#include <string>
#include <thread>

wxDEFINE_EVENT(wxEVT_SESSION_SUMMARY_READY, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_SUMMARY_FAILED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_HANDOFF_DELIVERED, HandoffDeliveryEvent);
wxDEFINE_EVENT(wxEVT_HANDOFF_DELIVERY_FAILED, HandoffDeliveryEvent);

namespace {
// The folder of all the handoffs in the folder of the source session. The file
// that the new session reads is in its folder itself, not in a subfolder: its
// agent deletes it when it has read it, and nothing is left. Its name has the
// id of the delivery (HandoffDelivery::m_fileName).
const wxString kHandoffRoot = ".kennel/handoff";
const wxString kTargetSummaryPattern = "kennel-handoff-*.md";
// The text after a paste is a new line in most agents until the paste is done:
// Enter comes a little later (as Review Buddy does).
constexpr int kEnterDelayMs = 400;
constexpr int kLocalPollMs = 1000;
constexpr int kRemotePollMs = 3000;
// How long the agent has to write the summary.
constexpr int kTimeoutMinutes = 5;
} // namespace

// ---------------------------------------------------------------------------
// HandoffPlace
// ---------------------------------------------------------------------------

HandoffPlace::HandoffPlace(const AgentDef &agent, const wxString &workingDir) {
  if (agent.IsRemote()) {
    remoteHost = agent.remoteHost;
    remoteUser = agent.remoteUser;
    remoteDir = workingDir.empty() ? wxString("~") : workingDir;
    agentDir = remoteDir; // Until SetRemoteDir()
  } else if (agent.IsWSL()) {
    // Through the share of the distribution (as Review Buddy reaches it); the
    // agent sees the Linux path.
    const wxString distro = wsl::DistroOf(agent.loginShell);
    // A Windows path (C:\..., the default folder of a session, or a path on
    // the share of the distribution) is made a Linux one; only a Linux path is
    // resolved (~, a relative path).
    wxString linuxDir;
    if (!wsl::ToLinuxPath(distro, workingDir, &linuxDir)) {
      linuxDir = wsl::ResolveLinuxPath(distro, workingDir, wxEmptyString);
    }
    if (!linuxDir.empty()) {
      localDir = wsl::ToWindowsPath(distro, linuxDir);
      agentDir = linuxDir;
    }
  } else {
#ifdef __WXMSW__
    posixShell = agent.IsBash();
#endif
    if (!workingDir.empty()) {
      wxFileName dir = wxFileName::DirName(workingDir);
      dir.Normalize(wxPATH_NORM_TILDE | wxPATH_NORM_ABSOLUTE |
                    wxPATH_NORM_DOTS);
      localDir = dir.GetPath();
      // Windows takes `/` too: the command line quotes a `\` (it would be
      // doubled), never a `/`.
      agentDir = localDir;
      agentDir.Replace("\\", "/");
    }
  }
}

bool HandoffPlace::IsSafeOnCommandLine(const wxString &text) const {
  if (posixShell) {
    return true;
  }
  for (const wxUniChar c : text) {
    if (c == '%' || c == '!' || c == '$' || c == '`' || c == '"') {
      return false;
    }
  }
  return true;
}

wxString HandoffPlace::GetAgentPath(const wxString &relPath) const {
  return agentDir.EndsWith("/") || agentDir.EndsWith("\\")
             ? agentDir + relPath
             : agentDir + "/" + relPath;
}

void HandoffPlace::SetRemoteDir(const wxString &dir) {
  remoteDir = dir;
  agentDir = dir;
}

wxString HandoffPlace::GetPath(const wxString &relPath) const {
  const wxString &dir = IsRemote() ? remoteDir : localDir;
  return dir.EndsWith("/") || dir.EndsWith("\\") ? dir + relPath
                                                 : dir + "/" + relPath;
}

void HandoffPlace::AddGitExclude(const wxString &pattern) const {
  if (IsRemote() || localDir.empty() || !wxDir::Exists(GetPath(".git"))) {
    return;
  }
  const wxString exclude = GetPath(".git/info/exclude");
  wxString content;
  if (wxFileName::FileExists(exclude)) {
    wxFFile in(exclude, "rb");
    if (in.IsOpened()) {
      in.ReadAll(&content, wxConvUTF8);
    }
  }
  if (content.Contains(pattern)) {
    return;
  }
  wxFileName::Mkdir(GetPath(".git/info"), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  wxFFile out(exclude, "ab");
  if (out.IsOpened()) {
    out.Write(wxString(content.empty() || content.EndsWith("\n") ? "" : "\n") +
                  pattern + "\n",
              wxConvUTF8);
  }
}

// ---------------------------------------------------------------------------
// SessionHandoff
// ---------------------------------------------------------------------------

SessionHandoff::SessionHandoff(const HandoffPlace &source,
                               wxTerminalViewCtrl *terminal)
    : m_source(source), m_terminal(terminal),
      m_id(wxDateTime::UNow().Format("%Y%m%d-%H%M%S-%l")) {
  m_enterTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &SessionHandoff::OnEnterTimer, this, m_enterTimer.GetId());
  m_pollTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &SessionHandoff::OnPollTimer, this, m_pollTimer.GetId());
}

SessionHandoff::~SessionHandoff() {
  Stop();
  m_alive->store(false);
}

void SessionHandoff::Stop() {
  if (m_done) {
    return;
  }
  m_done = true;
  m_pollTimer.Stop();
  m_enterTimer.Stop(); // An Enter that waits would send the prompt
  if (m_promptSent && !m_summaryFound && m_terminal != nullptr) {
    m_terminal->SendCtrlC();
  }
  DeleteSourceFiles();
}

wxString SessionHandoff::GetHandoffDir() const {
  return kHandoffRoot + "/" + m_id;
}

wxString SessionHandoff::GetPrompt() const {
  // One line: a new line would send the prompt in most agents.
  const wxString dir = GetHandoffDir();
  return wxString::Format(
      "Write a handoff summary of this session for another agent that will "
      "continue the work in a new session. Include: the goal, what was done, "
      "the decisions made and why, the current state, the files that "
      "changed, open problems, and the next steps. Write it as Markdown to "
      "the file %s, then create the empty file %s. Do not add these files to "
      "git, and do nothing else.",
      m_source.GetAgentPath(dir + "/summary.md"),
      m_source.GetAgentPath(dir + "/done"));
}

void SessionHandoff::Begin() {
  if (m_terminal == nullptr) {
    Fail(_("The session has no terminal"));
    return;
  }
  if (!m_source.IsRemote()) {
    if (m_source.localDir.empty()) {
      Fail(_("Cannot find the working directory of the session"));
      return;
    }
    m_source.AddGitExclude(kHandoffRoot + "/");
    SendPrompt();
    return;
  }
  // The prompt has the absolute path: `~` and `$HOME` of the folder are the
  // home of the host.
  m_ioBusy = true;
  const wxString host = m_source.remoteHost;
  const wxString user = m_source.remoteUser;
  const wxString dir = m_source.remoteDir;
  std::thread([this, alive = m_alive, host, user, dir] {
    const StatusOr<wxString> resolved =
        SftpClient::ResolvePath(host, user, dir);
    const wxString path = resolved.ok() ? resolved.value() : wxString();
    const wxString error =
        resolved.ok() ? wxString() : resolved.status().message();
    CallAfterIfAlive(alive, [this, path, error] {
      m_ioBusy = false;
      if (m_done) {
        return; // Cancel() ended it already
      }
      if (!error.empty()) {
        Fail(error);
        return;
      }
      m_source.SetRemoteDir(path);
      SendPrompt();
    });
  }).detach();
}

void SessionHandoff::SendPrompt() {
  m_deadline = wxDateTime::Now() + wxTimeSpan::Minutes(kTimeoutMinutes);
  m_promptSent = true;
  m_terminal->SendInput(GetPrompt().ToStdString(wxConvUTF8));
  m_enterTimer.StartOnce(kEnterDelayMs);
  m_pollTimer.Start(m_source.IsRemote() ? kRemotePollMs : kLocalPollMs);
}

void SessionHandoff::OnEnterTimer(wxTimerEvent &) {
  if (m_terminal != nullptr) {
    m_terminal->SendEnter();
  }
}

void SessionHandoff::Cancel() { Stop(); }

void SessionHandoff::OnPollTimer(wxTimerEvent &) {
  if (m_done || m_ioBusy) {
    return;
  }
  if (wxDateTime::Now() > m_deadline) {
    Fail(
        wxString::Format(_("The agent did not write the summary in %d minutes"),
                         kTimeoutMinutes));
    return;
  }
  if (m_source.IsRemote()) {
    CheckRemote();
  } else {
    CheckLocal();
  }
}

void SessionHandoff::CheckLocal() {
  const wxString dir = GetHandoffDir();
  if (!wxFileName::FileExists(m_source.GetPath(dir + "/done"))) {
    return;
  }
  // The marker says that the agent finished: it is not interrupted, also
  // when the summary can not be read.
  m_summaryFound = true;
  // All of it, or nothing: a part of the summary is not used.
  const wxString path = m_source.GetPath(dir + "/summary.md");
  wxString summary;
  wxFFile file(path, "rb");
  if (!file.IsOpened() || !file.ReadAll(&summary, wxConvUTF8)) {
    Fail(wxString::Format(_("Could not read '%s'"), path));
    return;
  }
  OnSummary(summary);
}

void SessionHandoff::CheckRemote() {
  m_ioBusy = true;
  const wxString host = m_source.remoteHost;
  const wxString user = m_source.remoteUser;
  const wxString marker = m_source.GetPath(GetHandoffDir() + "/done");
  const wxString file = m_source.GetPath(GetHandoffDir() + "/summary.md");
  std::thread([this, alive = m_alive, host, user, marker, file] {
    const StatusOr<bool> exists = SftpClient::Exists(host, user, marker);
    bool found = false;
    wxString summary;
    wxString error;
    if (!exists.ok()) {
      error = exists.status().message();
    } else if (exists.value()) {
      found = true;
      auto read = SftpClient::ReadFile(host, user, file, {});
      if (read.ok()) {
        summary = wxString::FromUTF8(read.value().content);
      } else {
        error = read.status().message();
      }
    }
    CallAfterIfAlive(alive, [this, found, summary, error] {
      m_ioBusy = false;
      if (m_done) {
        return; // Cancelled (or ended) while the check ran
      }
      if (found) {
        // The marker says that the agent finished: it is not interrupted,
        // also when the summary can not be read.
        m_summaryFound = true;
      }
      if (!error.empty()) {
        // A check that failed is tried again by the next tick (the host may
        // be slow); a summary that could not be read ends the handoff.
        KLOG_WARN() << "Handoff: " << error;
        if (found) {
          Fail(error);
        }
        return;
      }
      if (found) {
        OnSummary(summary);
      }
    });
  }).detach();
}

void SessionHandoff::OnSummary(const wxString &summary) {
  m_summaryFound = true;
  Stop(); // The files go; the agent finished, it is not interrupted
  wxString text = summary;
  text.Trim().Trim(false);
  if (text.empty()) {
    Fail(_("The agent wrote an empty summary"));
    return;
  }
  wxCommandEvent ready{wxEVT_SESSION_SUMMARY_READY};
  ready.SetString(summary);
  AddPendingEvent(ready);
}

void SessionHandoff::DeleteSourceFiles() {
  if (!m_source.IsRemote() && m_source.localDir.empty()) {
    return; // No folder: nothing was written
  }
  const wxString dir = GetHandoffDir();
  if (!m_source.IsRemote()) {
    const wxString folder = m_source.GetPath(dir);
    if (wxDir::Exists(folder) &&
        !wxFileName::Rmdir(folder, wxPATH_RMDIR_RECURSIVE)) {
      KLOG_WARN() << "Handoff: could not remove '" << folder << "'";
    }
    // The folders around it, when nothing else is in them.
    for (const wxString &parent :
         {m_source.GetPath(kHandoffRoot), m_source.GetPath(".kennel")}) {
      wxDir parentDir(parent);
      if (parentDir.IsOpened() && !parentDir.HasFiles() &&
          !parentDir.HasSubDirs()) {
        parentDir.Close();
        wxFileName::Rmdir(parent);
      }
    }
    return;
  }
  // The folder of this handoff only (its name has the id), and its parents
  // when they are empty. Nobody waits for it.
  const wxString host = m_source.remoteHost;
  const wxString user = m_source.remoteUser;
  const wxString path = m_source.GetPath(dir);
  const std::vector<wxString> parents{m_source.GetPath(kHandoffRoot),
                                      m_source.GetPath(".kennel")};
  // Kennel waits for it at exit (PromptFileRemover::Flush()).
  PromptFileRemover::Get().BeginJob();
  std::thread([host, user, path, parents] {
    const Status st = SftpClient::RemoveTree(host, user, path, parents);
    if (!st.ok()) {
      KLOG_WARN() << "Handoff: could not remove '" << path
                  << "': " << st.message();
    }
    PromptFileRemover::Get().EndJob();
  }).detach();
}

void SessionHandoff::Fail(const wxString &why) {
  Stop();
  KLOG_WARN() << "Handoff: no summary: " << why;
  wxCommandEvent failed{wxEVT_SESSION_SUMMARY_FAILED};
  failed.SetString(why);
  AddPendingEvent(failed);
}

// ---------------------------------------------------------------------------
// HandoffDelivery
// ---------------------------------------------------------------------------

HandoffDelivery::HandoffDelivery(const HandoffPlace &target,
                                 const wxString &summary,
                                 const NewSessionRequest &request)
    : m_target(target), m_summary(summary), m_request(request),
      m_fileName(
          wxString::Format("kennel-handoff-%s.md",
                           wxDateTime::UNow().Format("%Y%m%d-%H%M%S-%l"))) {}

HandoffDelivery::~HandoffDelivery() {
  m_alive->store(false);
  // A remote file whose result did not come here yet (it is queued): nobody
  // takes it. A worker that still writes sees ownerGone, and deletes it.
  wxString unhandled;
  {
    std::lock_guard<std::mutex> lock(m_remoteWrite->mutex);
    m_remoteWrite->ownerGone = true;
    if (!m_remoteWrite->handled) {
      unhandled = m_remoteWrite->path;
    }
  }
  if (!unhandled.empty()) {
    PromptFileRemover::Get().Remove(
        PromptFile{unhandled, m_target.remoteHost, m_target.remoteUser});
  }
  // A summary that no page took (the session did not start, or the notebook
  // went first) is not read by any agent.
  Undo();
}

void HandoffDelivery::Begin() {
  if (!m_target.IsRemote()) {
    if (m_target.localDir.empty()) {
      Fail(_("Cannot find the working directory of the new session"));
      return;
    }
    wxFileName fn(m_target.GetPath(m_fileName));
    // Nothing of the user is replaced, and a symbolic link is not followed:
    // the path must be free, and "x" creates the file only if it is not
    // there.
    if (wxFileName::Exists(fn.GetFullPath(),
                           wxFILE_EXISTS_ANY | wxFILE_EXISTS_NO_FOLLOW)) {
      Fail(wxString::Format(_("'%s' exists already"), fn.GetFullPath()));
      return;
    }
    const wxString agentPath = m_target.GetAgentPath(m_fileName);
    if (!m_target.IsSafeOnCommandLine(agentPath)) {
      Fail(
          wxString::Format(_("The shell of the agent would change the path "
                             "'%s': choose a folder without %%, !, $, ` or \""),
                           agentPath));
      return;
    }
    // Mkdir() of a wxFileName makes its folder part (GetPath()), not the file.
    wxFFile file;
    if (!fn.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) ||
        !file.Open(fn.GetFullPath(), "wbx")) {
      Fail(wxString::Format(_("Could not create '%s'"), fn.GetFullPath()));
      return;
    }
    // Created: it is this delivery's, and goes on any error from now on.
    m_writtenPath = fn.GetFullPath();
    const bool written = file.Write(m_summary, wxConvUTF8);
    // Close() flushes: an error there is a write error too.
    if (!file.Close() || !written) {
      Undo();
      Fail(wxString::Format(_("Could not write '%s'"), fn.GetFullPath()));
      return;
    }
    m_target.AddGitExclude(kTargetSummaryPattern);
    Succeed(agentPath, fn.GetFullPath());
    return;
  }
  // The folder is resolved first: the first prompt has the absolute path.
  const wxString host = m_target.remoteHost;
  const wxString user = m_target.remoteUser;
  const wxString dir = m_target.remoteDir;
  const wxString name = m_fileName;
  const std::string content = m_summary.ToStdString(wxConvUTF8);
  // Kennel waits for this worker at exit (PromptFileRemover::Flush()).
  PromptFileRemover::Get().BeginJob();
  std::thread([this, state = m_remoteWrite, host, user, dir, name, content] {
    wxString path;
    wxString error;
    const StatusOr<wxString> resolved =
        SftpClient::ResolvePath(host, user, dir);
    if (!resolved.ok()) {
      error = resolved.status().message();
    } else {
      path = resolved.value().EndsWith("/") ? resolved.value() + name
                                            : resolved.value() + "/" + name;
      // Exclusive: nothing that is at the path is replaced.
      const Status st = SftpClient::PutFile(host, user, path, content,
                                            /*exclusive=*/true);
      if (!st.ok()) {
        error = st.message();
      }
    }
    const bool written = error.empty() && !path.empty();
    bool orphan = false;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (written) {
        state->path = path;
      }
      orphan = state->ownerGone || wxTheApp == nullptr;
    }
    if (orphan) {
      // The delivery went while this wrote: nobody starts the session.
      if (written) {
        PromptFileRemover::Get().Remove(PromptFile{path, host, user});
      }
    } else {
      // Its destructor deletes the file if this does not run first.
      wxTheApp->CallAfter([this, state, path, error] {
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          if (state->ownerGone) {
            return;
          }
          state->handled = true;
        }
        if (!error.empty()) {
          Fail(error);
          return;
        }
        Succeed(path, path);
      });
    }
    PromptFileRemover::Get().EndJob();
  }).detach();
}

void HandoffDelivery::Undo() {
  if (m_writtenPath.empty()) {
    return;
  }
  // The file this delivery created (its name has the id); the page of the
  // session may have deleted it already.
  PromptFileRemover::Get().Remove(
      PromptFile{m_writtenPath, m_target.remoteHost, m_target.remoteUser});
  m_writtenPath.clear();
}

void HandoffDelivery::ReleaseFile() { m_writtenPath.clear(); }

void HandoffDelivery::Succeed(const wxString &agentPath,
                              const wxString &writtenPath) {
  m_writtenPath = writtenPath;
  // The page of the new session deletes it when it goes (ReleaseFile() hands
  // it over).
  m_request.promptFile =
      PromptFile{writtenPath, m_target.remoteHost, m_target.remoteUser};
  m_request.initialPrompt = wxString::Format(
      "Read the file %s: it is a summary of an earlier session, written by "
      "its agent. When you have read it, delete the file, and do not add it "
      "to git. Then continue the work from it.",
      agentPath);
  HandoffDeliveryEvent delivered{wxEVT_HANDOFF_DELIVERED};
  delivered.SetEventObject(this);
  delivered.SetRequest(m_request);
  AddPendingEvent(delivered);
}

void HandoffDelivery::Fail(const wxString &why) {
  KLOG_WARN() << "Handoff to '" << m_request.name << "' failed: " << why;
  HandoffDeliveryEvent failed{wxEVT_HANDOFF_DELIVERY_FAILED};
  failed.SetEventObject(this);
  failed.SetRequest(m_request);
  failed.SetString(why);
  AddPendingEvent(failed);
}
