#include "core/RemoteInstaller.h"

#include "core/EventNotifier.hpp"
#include "core/Logger.h"
#include "core/Process.hpp"
#include "core/SftpClient.h"
#include "core/Sha256.h"
#include "core/ShellQuote.h"
#include "core/SshClient.h"
#include "core/TarGz.h"
#include "core/WslPath.h"
#include "core/ZipArchive.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stream.h>
#include <wx/utils.h>
#include <wx/webrequest.h>

wxDEFINE_EVENT(wxEVT_INSTALL_PROGRESS, InstallEvent);
wxDEFINE_EVENT(wxEVT_INSTALL_DONE, InstallEvent);

namespace {

constexpr auto kShortTimeout = std::chrono::seconds(60);

// Set by RemoteInstaller::Stop(): the application is going away, so nothing is
// queued on the EventNotifier or on a handler any more (they are destroyed with
// the process, and a thread that is still running must not use them then).
std::atomic<bool> g_stopped{false};

// Runs `fn` on the UI thread, through the EventNotifier: a handler that lives
// as long as the process. Dropped once stopped.
template <typename Fn> void CallOnUiThread(const Fn &fn) {
  if (!g_stopped.load()) {
    EventNotifier::Get()->CallAfter(fn);
  }
}

wxString Text(const std::string &text) {
  wxString decoded = wxString::FromUTF8(text.data(), text.size());
  decoded.Trim().Trim(false);
  return decoded;
}

// What a shell command printed and how it ended.
struct ShellResult {
  int exitCode{-1};
  std::string out;
  std::string err;
};

// Runs `script` with `sh -c` on the machine of `account`. An error status only
// when it could not run at all (no ssh connection, no wsl.exe); a script that
// exits with a code other than 0 is a result, for the caller to judge.
// Blocking.
//
// `timeout` is for a remote host only: a program started here is waited for.
StatusOr<ShellResult> RunShell(const Account &account,
                               const std::string &script,
                               std::chrono::seconds timeout) {
  if (account.IsRemote()) {
    auto reply = SshClient::Execute(
        account.m_name, account.m_user,
        wxString::FromUTF8("sh -c " + ShellQuote(script)), timeout);
    if (!reply.ok()) {
      return reply.status();
    }
    ShellResult result;
    result.exitCode = reply->exitCode;
    result.out = reply->out;
    result.err = reply->err;
    return result;
  }

  std::vector<std::string> argv;
  if (account.IsWSL()) {
    argv = {"wsl.exe", "-d", account.m_name.ToStdString(wxConvUTF8), "--exec"};
  }
  argv.push_back("sh");
  argv.push_back("-c");
  argv.push_back(script);
  // On Windows the program gets one command line: the script has spaces.
  for (std::string &arg : argv) {
    arg = Process::QuoteArgument(arg);
  }
  ShellResult result;
  result.exitCode = Process::RunProcessAndWait(
      argv, [&](const std::string &out, const std::string &err) {
        result.out += out;
        result.err += err;
        return true;
      });
  // -1 is what RunProcessAndWait returns when the program could not be started.
  // Any other number is an exit code, also a negative one: on Windows a program
  // that was ended by an error (an NTSTATUS such as 0xC0000005) has one, and it
  // is for the caller to judge like any other code that is not 0.
  if (result.exitCode == -1) {
    return Status::Error(wxString::Format(_("Could not run a command on %s"),
                                          account.GetDisplayName()));
  }
  return result;
}

// This machine is Windows: it has no `sh`, `uname`, `mkdir -p` or `chmod`, so
// the steps that use them have a way of their own for it (DetectMachine(),
// VersionMatches(), Upload()). A WSL distribution and a remote host are not
// Windows, whatever this machine is.
bool IsLocalWindows(const Account &account) {
#ifdef __WXMSW__
  return account.IsLocalHost();
#else
  wxUnusedVar(account);
  return false;
#endif
}

struct Machine {
  wxString os;   // `uname -s`: "Linux", "Darwin"
  wxString arch; // `uname -m`, as ToolAsset names it: "x86_64", "aarch64"
};

// `uname -m` is not the same everywhere: macOS on Apple Silicon says "arm64"
// where Linux says "aarch64", and some systems say "amd64" for "x86_64".
wxString NormalizedArch(const wxString &arch) {
  if (arch == "arm64") {
    return "aarch64";
  }
  if (arch == "amd64") {
    return "x86_64";
  }
  return arch;
}

StatusOr<Machine> DetectMachine(const Account &account) {
  if (IsLocalWindows(account)) {
    // The CPU of the machine, not the one of the Kennel that runs (an x86_64
    // Kennel on an ARM machine gets the ARM program).
    return Machine{"Windows", NormalizedArch(wxGetNativeCpuArchitectureName())};
  }
  auto reply = RunShell(account, "uname -s; uname -m", kShortTimeout);
  if (!reply.ok()) {
    return reply.status();
  }
  const wxString out = wxString::FromUTF8(reply->out);
  Machine machine;
  machine.os = out.BeforeFirst('\n').Trim().Trim(false);
  machine.arch =
      NormalizedArch(out.AfterFirst('\n').BeforeFirst('\n').Trim().Trim(false));
  if (reply->exitCode != 0 || machine.os.empty() || machine.arch.empty()) {
    return Status::Error(
        wxString::Format(_("Could not find out what kind of machine %s is: %s"),
                         account.GetDisplayName(), Text(reply->err)));
  }
  return machine;
}

// `dir` without the slashes at its end ("/" stays).
wxString WithoutTrailingSlashes(wxString dir) {
  while (dir.length() > 1 && dir.EndsWith("/")) {
    dir.RemoveLast();
  }
  return dir;
}

// The path of the file `name` in the folder `dir` (as WithoutTrailingSlashes()
// leaves it).
wxString JoinPath(const wxString &dir, const wxString &name) {
  return dir.EndsWith("/") ? dir + name : dir + "/" + name;
}

// Whether `path` starts at the home folder: `~`, `$HOME`, or one of them
// followed by a path. `rest` gets what comes after the home: empty, or the path
// with its slash ("/.kennel/bin").
bool SplitHome(const wxString &path, wxString *rest) {
  for (const char *name : {"~", "$HOME"}) {
    const wxString home(name);
    if (path == home) {
      rest->clear();
      return true;
    }
    if (path.StartsWith(home + "/")) {
      *rest = path.Mid(home.length());
      return true;
    }
  }
  return false;
}

// Whether `dir` is a folder that the installer can name on every kind of
// machine: absolute, or under the home folder (`~`, `$HOME`). A relative one
// would mean another folder for ssh (where the shell starts) than for this
// process (WSL, this machine).
bool IsSupportedDir(const wxString &dir) {
  wxString rest;
  return dir.StartsWith("/") || SplitHome(dir, &rest);
}

// The home folder of the user, for the machines whose files this process
// reaches itself (a WSL share, this machine). Empty if unknown.
wxString HomeOf(const Account &account) {
  if (account.IsWSL()) {
    return wsl::HomeDir(account.m_name);
  }
  return wxFileName::GetHomeDir();
}

// `path` (absolute, or with a leading `~` or `$HOME`) as an absolute Linux
// path. Empty if the home folder is not known or the path is relative.
wxString AbsolutePath(const Account &account, const wxString &path) {
  wxString rest;
  if (SplitHome(path, &rest)) {
    const wxString home = HomeOf(account);
    return home.empty() ? wxString() : home + rest;
  }
  return path.StartsWith("/") ? path : wxString();
}

// `path` (absolute, or under the home folder) as a path of this machine on
// Windows. Empty if the home folder is not known.
wxString WindowsPath(const Account &account, const wxString &path) {
  wxString native = AbsolutePath(account, path);
  native.Replace("/", "\\");
  return native;
}

// `path` as a word for the shell of the machine. A leading `~` or `$HOME` is
// the home of the user there: it becomes "$HOME" in double quotes, which the
// shell expands (in single quotes it would not).
//
// Not for WSL: wsl.exe reads its own command line, and a double quote in it is
// not safe. The home is written out there, so the script has no double quote at
// all (and ShellQuote makes only single quotes).
std::string ShellPath(const Account &account, const wxString &path) {
  wxString rest;
  if (!SplitHome(path, &rest)) {
    return ShellQuote(path.ToStdString(wxConvUTF8));
  }
  if (account.IsWSL()) {
    const wxString absolute = AbsolutePath(account, path);
    if (!absolute.empty()) {
      return ShellQuote(absolute.ToStdString(wxConvUTF8));
    }
  }
  if (rest.empty()) {
    return "\"$HOME\"";
  }
  return "\"$HOME\"/" + ShellQuote(rest.Mid(1).ToStdString(wxConvUTF8));
}

// Whether `path` is the program, in the version of `banner`. Not being there is
// no error, just "no".
StatusOr<bool> VersionMatches(const Account &account, const wxString &path,
                              const wxString &banner) {
  if (IsLocalWindows(account)) {
    // No shell: the program is run itself.
    const wxString native = WindowsPath(account, path);
    if (native.empty()) {
      return Status::Error(wxString::Format(
          _("Could not find the home folder on %s"), account.GetDisplayName()));
    }
    if (!wxFileExists(native)) {
      return false;
    }
    std::string out;
    const int code = Process::RunProcessAndWait(
        {Process::QuoteArgument(native.ToStdString(wxConvUTF8)), "--version"},
        [&](const std::string &text, const std::string &) {
          out += text;
          return true;
        });
    if (code != 0) {
      return false;
    }
    return RemoteInstaller::MatchesBanner(wxString::FromUTF8(out), banner);
  }
  auto reply =
      RunShell(account, ShellPath(account, path) + " --version 2>/dev/null",
               kShortTimeout);
  if (!reply.ok()) {
    return reply.status();
  }
  if (reply->exitCode != 0) {
    return false;
  }
  return RemoteInstaller::MatchesBanner(wxString::FromUTF8(reply->out), banner);
}

// Writes `data` to the file `path` on the machine. The folder is there.
Status WriteFile(const Account &account, const wxString &path,
                 const std::string &data) {
  if (account.IsRemote()) {
    return SftpClient::PutFile(account.m_name, account.m_user, path, data);
  }
  const wxString absolute = AbsolutePath(account, path);
  if (absolute.empty()) {
    return Status::Error(wxString::Format(
        _("Could not find the home folder on %s"), account.GetDisplayName()));
  }
  // A WSL distribution is written through its share, this machine directly.
  const wxString native =
      account.IsWSL() ? wsl::ToWindowsPath(account.m_name, absolute) : absolute;
  wxFile file;
  if (!file.Create(native, /*overwrite=*/true)) {
    return Status::Error(wxString::Format(_("Could not create %s"), native));
  }
  if (file.Write(data.data(), data.size()) != data.size() || !file.Close()) {
    return Status::Error(wxString::Format(_("Could not write %s"), native));
  }
  return Status::Ok();
}

// Puts `data` in the file `path`, in the folder `dir`, and makes it executable.
// It is written under another name first and renamed, which is atomic, so a
// failure never leaves a file under the name of the program that is not the
// program.
Status Upload(const Account &account, const wxString &dir, const wxString &path,
              const std::string &data) {
  const wxString tempPath = path + ".kennel-tmp";

  if (IsLocalWindows(account)) {
    // The same steps without a shell: a folder, the file under another name,
    // and a rename over the program (a program has no mode to set here).
    const wxString nativeDir = WindowsPath(account, dir);
    const wxString nativePath = WindowsPath(account, path);
    if (nativeDir.empty() || nativePath.empty()) {
      return Status::Error(wxString::Format(
          _("Could not find the home folder on %s"), account.GetDisplayName()));
    }
    if (!wxFileName::Mkdir(nativeDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
      return Status::Error(
          wxString::Format(_("Could not create the folder %s"), nativeDir));
    }
    const wxString nativeTemp = nativePath + ".kennel-tmp";
    Status written = WriteFile(account, tempPath, data);
    if (!written.ok()) {
      wxRemoveFile(nativeTemp); // Best effort
      return written;
    }
    if (!wxRenameFile(nativeTemp, nativePath, /*overwrite=*/true)) {
      wxRemoveFile(nativeTemp);
      // Windows does not replace a program that is running: a search that
      // runs, or an rg that something else started.
      return Status::Error(wxString::Format(
          _("Could not install %s (is it running?)"), nativePath));
    }
    return Status::Ok();
  }
  const std::string temp = ShellPath(account, tempPath);

  auto made =
      RunShell(account, "mkdir -p " + ShellPath(account, dir), kShortTimeout);
  if (!made.ok()) {
    return made.status();
  }
  if (made->exitCode != 0) {
    return Status::Error(wxString::Format(
        _("Could not create the folder %s: %s"), dir, Text(made->err)));
  }

  Status written = WriteFile(account, tempPath, data);
  if (!written.ok()) {
    RunShell(account, "rm -f " + temp, kShortTimeout); // Best effort
    return written;
  }

  auto moved = RunShell(account,
                        "chmod 755 " + temp + " && mv -f " + temp + " " +
                            ShellPath(account, path) + " || { rm -f " + temp +
                            "; exit 1; }",
                        kShortTimeout);
  if (!moved.ok()) {
    return moved.status();
  }
  if (moved->exitCode != 0) {
    return Status::Error(wxString::Format(_("Could not install %s: %s"), path,
                                          Text(moved->err)));
  }
  return Status::Ok();
}

} // namespace

bool RemoteInstaller::MatchesBanner(const wxString &output,
                                    const wxString &banner) {
  if (banner.empty()) {
    return false;
  }
  wxString first = output.BeforeFirst('\n');
  first.Trim().Trim(false);
  return first == banner || first.StartsWith(banner + " ");
}

// The work of one install. Shared (std::shared_ptr) by the RemoteInstaller that
// started it, the threads that run its steps, the calls that wait for the UI
// thread, and, during the download, the web request. So it lives as long as any
// of them needs it, whatever happens to the RemoteInstaller: that one can be
// destroyed at any time without waiting.
//
// It is the handler of its own web request (wxWebRequest keeps a plain pointer
// to its handler, and sends it events later), which is why it is a
// wxEvtHandler and why it holds itself alive while the download runs.
//
// Where a call may come from: the functions marked "UI thread" run on it only.
// The ones marked "Worker" run on a thread of their own and never touch what is
// marked "UI thread only".
class RemoteInstaller::Job : public wxEvtHandler,
                             public std::enable_shared_from_this<Job> {
public:
  Job(wxEvtHandler *handler, const Account &account,
      const ToolDescription &tool, const wxString &targetDir, Mode mode)
      : m_account(account), m_tool(tool), m_mode(mode),
        m_dir(WithoutTrailingSlashes(targetDir)),
        m_path(JoinPath(m_dir, IsLocalWindows(account) ? tool.binary + ".exe"
                                                       : tool.binary)),
        m_handler(handler) {
    Bind(wxEVT_WEBREQUEST_STATE, &Job::OnRequestState, this);
    Bind(wxEVT_WEBREQUEST_DATA, &Job::OnData, this);
  }

  // UI thread.
  void Launch() {
    m_running = true;
    // Detached: nobody waits for a thread that may be in an ssh call.
    std::thread([job = shared_from_this()] { job->Detect(); }).detach();
  }

  // UI thread. After this no event is sent to the handler.
  void Detach() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_handler = nullptr;
    }
    Cancel();
  }

  // UI thread.
  void Cancel() {
    if (!m_running) {
      return;
    }
    m_cancel = true;
    if (m_request.IsOk()) {
      // Ends in State_Cancelled, which reports.
      m_request.Cancel();
    }
  }

  // UI thread. True from Launch() until the end is decided (Finish()), which is
  // a moment before the event of the end reaches the handler.
  bool IsRunning() const { return m_running; }

private:
  // Worker: what is the machine, and is the program there already?
  void Detect() {
    Progress(wxString::Format(_("Looking at %s"), m_account.GetDisplayName()));
    auto machine = DetectMachine(m_account);
    if (!machine.ok()) {
      Report(machine.status());
      return;
    }
    const ToolAsset *asset = m_tool.FindAsset(machine->os, machine->arch);
    if (asset == nullptr) {
      Report(Status::Error(wxString::Format(_("%s is not available for %s %s"),
                                            m_tool.name, machine->os,
                                            machine->arch)));
      return;
    }

    auto installed = VersionMatches(m_account, m_path, m_tool.versionBanner);
    if (!installed.ok()) {
      Report(installed.status());
      return;
    }
    if (*installed) {
      Report(Status::Ok(), /*alreadyInstalled=*/true);
      return;
    }
    if (m_mode == Mode::kCheckOnly) {
      Complete(Status::Ok(), /*alreadyInstalled=*/false, /*cancelled=*/false,
               /*needsInstall=*/true);
      return;
    }
    if (m_cancel) {
      ReportCancelled();
      return;
    }
    // The download is a web request: it starts on the UI thread. The call is
    // queued on the EventNotifier, which lives as long as the process (this job
    // may not: it is a handler that nobody holds once the RemoteInstaller is
    // gone, and a call queued on it would be dropped with it). The call owns
    // the job, so the job is alive until the UI thread has run it, and is let
    // go there, never on this thread while the UI thread is using it. A job
    // that was detached meanwhile ends in BeginDownload() (it is cancelled).
    CallOnUiThread([job = shared_from_this(), asset = *asset] {
      job->BeginDownload(asset);
    });
  }

  // UI thread.
  void BeginDownload(const ToolAsset &asset) {
    if (m_cancel) {
      ReportCancelled();
      return;
    }
    m_asset = asset;
    Progress(
        wxString::Format(_("Downloading %s %s"), m_tool.name, m_tool.version));
    m_request = wxWebSession::GetDefault().CreateRequest(this, asset.url);
    if (!m_request.IsOk()) {
      Report(Status::Error(_("Could not create the download request")));
      return;
    }
    // The data comes to OnData() in pieces, as it arrives, so that a response
    // that is too big is stopped at the limit and not held in memory first.
    m_request.SetStorage(wxWebRequest::Storage_None);
    m_download.clear();
    m_tooBig = false;
    // The request calls back later, and only has a plain pointer to this.
    m_self = shared_from_this();
    m_request.Start();
  }

  // UI thread: a piece of the download.
  void OnData(wxWebRequestEvent &event) {
    if (m_tooBig) {
      return;
    }
    const wxFileOffset expected = m_request.IsOk()
                                      ? m_request.GetBytesExpectedToReceive()
                                      : wxInvalidOffset;
    m_download.append(static_cast<const char *>(event.GetDataBuffer()),
                      event.GetDataSize());
    if (m_download.size() > RemoteInstaller::kMaxDownload ||
        (expected > 0 &&
         static_cast<size_t>(expected) > RemoteInstaller::kMaxDownload)) {
      m_tooBig = true;
      m_download.clear();
      m_download.shrink_to_fit();
      if (m_request.IsOk()) {
        // Ends in State_Cancelled, which says why.
        m_request.Cancel();
      }
    }
  }

  // UI thread.
  void OnRequestState(wxWebRequestEvent &event) {
    switch (event.GetState()) {
    case wxWebRequest::State_Completed:
      OnDownloaded(event.GetResponse());
      return;
    case wxWebRequest::State_Failed:
    case wxWebRequest::State_Unauthorized:
      EndRequest();
      if (m_tooBig) {
        // A backend may end the transfer that OnData() cancelled as a failure:
        // the reason is still that it was too big.
        ReportTooBig();
      } else {
        Report(Status::Error(
            wxString::Format(_("The download of %s failed: %s"), m_tool.name,
                             event.GetErrorDescription())));
      }
      return;
    case wxWebRequest::State_Cancelled:
      EndRequest();
      if (m_tooBig) {
        ReportTooBig();
      } else {
        ReportCancelled();
      }
      return;
    default:
      // Idle or Active: still in flight.
      return;
    }
  }

  // UI thread.
  void OnDownloaded(const wxWebResponse &response) {
    EndRequest();
    // The last piece can be the one that went over the limit, with the end of
    // the download already on its way: Cancel() is too late then, and the
    // buffer was cleared, which is not "empty".
    if (m_tooBig) {
      ReportTooBig();
      return;
    }
    if (response.GetStatus() != 200) {
      Report(Status::Error(
          wxString::Format(_("The download of %s failed: %d %s"), m_tool.name,
                           response.GetStatus(), response.GetStatusText())));
      return;
    }
    if (m_download.empty()) {
      Report(Status::Error(_("The download is empty")));
      return;
    }
    // The rest needs ssh and the disk: not on this thread.
    std::thread([job = shared_from_this(),
                 data = std::move(m_download)]() mutable {
      job->Install(std::move(data));
    }).detach();
    m_download.clear();
  }

  // UI thread: the request is over, this job does not need to hold itself.
  void EndRequest() {
    m_request = wxWebRequest();
    // This runs inside an event of this object: releasing the last reference
    // here would delete it in the middle of its own handler. Let the loop do it
    // when the handler has returned.
    //
    // Not through CallOnUiThread(): that drops the call once Stop() was called,
    // and then the reset below would be the last reference, released in the
    // middle of the handler (the RemoteInstaller is gone by then, and its
    // request can still end). Here the event loop is running (this is one of
    // its events), so the EventNotifier exists and is safe to use. If the loop
    // never runs the call, the job is simply never freed: the process is
    // ending.
    if (m_self) {
      EventNotifier::Get()->CallAfter([keep = m_self] {});
    }
    m_self.reset();
  }

  // Worker: the download is in memory. Check it, take the program out, put it
  // on the machine, and look at it.
  void Install(std::string download) {
    Progress(_("Checking the download"));
    if (Sha256Hex(download) != m_asset.sha256.ToStdString()) {
      // Not a corrupt download to try again: the file is not the one that
      // Kennel was built for.
      Report(Status::Error(wxString::Format(
          _("The download of %s does not have the expected checksum. Nothing "
            "was installed."),
          m_tool.name)));
      return;
    }

    std::string program = std::move(download);
    if (!m_asset.member.empty()) {
      auto member = m_asset.url.EndsWith(".zip")
                        ? ReadZipMember(program, m_asset.member)
                        : ReadTarGzMember(program, m_asset.member);
      if (!member.ok()) {
        Report(member.status());
        return;
      }
      program = std::move(*member);
    }
    if (m_cancel) {
      ReportCancelled();
      return;
    }

    Progress(wxString::Format(_("Installing %s on %s"), m_tool.name,
                              m_account.GetDisplayName()));
    Status uploaded = Upload(m_account, m_dir, m_path, program);
    if (!uploaded.ok()) {
      Report(uploaded);
      return;
    }

    Progress(_("Checking the installed program"));
    auto installed = VersionMatches(m_account, m_path, m_tool.versionBanner);
    if (!installed.ok()) {
      Report(installed.status());
      return;
    }
    if (!*installed) {
      Report(Status::Error(wxString::Format(
          _("%s was installed, but it does not run here or is not version %s"),
          m_tool.name, m_tool.version)));
      return;
    }
    KLOG_INFO() << "Installed " << m_tool.name << " " << m_tool.version
                << " on " << m_account.GetDisplayName();
    Report(Status::Ok());
  }

  // Any thread. Sends the event to the handler, or drops it when the
  // RemoteInstaller is gone.
  void Post(wxEvent *event) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_handler != nullptr && !g_stopped.load()) {
      m_handler->QueueEvent(event);
    } else {
      delete event;
    }
  }

  // Any thread.
  void Progress(const wxString &text) {
    auto *event = new InstallEvent(wxEVT_INSTALL_PROGRESS);
    event->SetString(text);
    Post(event);
  }

  // The download went over kMaxDownload.
  void ReportTooBig() {
    Report(Status::Error(
        wxString::Format(_("The download of %s is too big"), m_tool.name)));
  }

  // Any thread. The end of the install. The event of the end is sent from the
  // UI thread (see Finish()), not from here.
  void Report(const Status &result, bool alreadyInstalled = false) {
    if (!result.ok()) {
      KLOG_WARN() << "Install " << m_tool.name << " on "
                  << m_account.GetDisplayName() << ": " << result.message();
    }
    Complete(result, alreadyInstalled, /*cancelled=*/false);
  }

  // Any thread.
  void ReportCancelled() {
    Complete(Status::Error(_("Cancelled")), /*alreadyInstalled=*/false,
             /*cancelled=*/true);
  }

  // Any thread: runs Finish() on the UI thread, through the EventNotifier (see
  // Detect()). The call owns the job, so it is alive until Finish() has run.
  void Complete(const Status &result, bool alreadyInstalled, bool cancelled,
                bool needsInstall = false) {
    CallOnUiThread([job = shared_from_this(), result, alreadyInstalled,
                    cancelled, needsInstall] {
      job->Finish(result, alreadyInstalled, cancelled, needsInstall);
    });
  }

  // UI thread. The install is over: IsRunning() turns false, and the event of
  // the end is queued for the handler. Between the two the UI thread may run
  // another event, so the handler does not know that IsRunning() is false when
  // it gets the event; the header says what a caller may rely on.
  void Finish(const Status &result, bool alreadyInstalled, bool cancelled,
              bool needsInstall) {
    m_running = false;
    auto *event = new InstallEvent(wxEVT_INSTALL_DONE);
    event->SetResult(result);
    event->SetAlreadyInstalled(alreadyInstalled);
    event->SetCancelled(cancelled);
    event->SetNeedsInstall(needsInstall);
    if (result.ok() && !needsInstall) {
      event->SetPath(m_path);
    }
    Post(event);
  }

  // What the job was given, and what is made of it. Never changes. `m_dir` is
  // the folder without the slashes at its end, `m_path` the program in it: the
  // one spelling that is used for the checks, the upload and the event.
  const Account m_account;
  const ToolDescription m_tool;
  const Mode m_mode;
  const wxString m_dir;
  const wxString m_path;

  // The handler of the events, null once the RemoteInstaller is gone. The
  // mutex makes "send, or drop" one step, so an event is never sent to a
  // handler that the owner has already let go.
  std::mutex m_mutex;
  wxEvtHandler *m_handler;

  // The asset that is downloaded. Written on the UI thread before the second
  // thread starts, read by it.
  ToolAsset m_asset;

  std::atomic<bool> m_cancel{false};
  // UI thread only. From Launch() until Finish().
  bool m_running{false};

  // UI thread only: the download (a request, the bytes so far, whether it went
  // over the limit), and the job holding itself while the request runs.
  wxWebRequest m_request;
  std::string m_download;
  bool m_tooBig{false};
  std::shared_ptr<Job> m_self;
};

void RemoteInstaller::Stop() { g_stopped.store(true); }

RemoteInstaller::RemoteInstaller(wxEvtHandler *handler) : m_handler(handler) {}

RemoteInstaller::~RemoteInstaller() {
  if (m_job) {
    m_job->Detach();
  }
}

bool RemoteInstaller::IsRunning() const {
  return m_job != nullptr && m_job->IsRunning();
}

void RemoteInstaller::Cancel() {
  if (m_job) {
    m_job->Cancel();
  }
}

Status RemoteInstaller::Start(const Account &account,
                              const ToolDescription &tool,
                              const wxString &targetDir, Mode mode) {
  if (IsRunning()) {
    return Status::Error(_("An installation is already running"));
  }
  if (tool.binary.empty() || targetDir.empty()) {
    return Status::Error(
        _("There is nothing to install, or nowhere to put it"));
  }
  if (!IsSupportedDir(targetDir)) {
    return Status::Error(
        _("The folder to install into must be an absolute path, or start with "
          "~ or $HOME"));
  }
  // The job of the last install may have a thread that is about to return: it
  // owns what it uses, so there is nothing to wait for.
  m_job = std::make_shared<Job>(m_handler, account, tool, targetDir, mode);
  m_job->Launch();
  return Status::Ok();
}
