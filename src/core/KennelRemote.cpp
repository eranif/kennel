#include "core/KennelRemote.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <chrono>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/thread.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

static const wxString kOutputEndDelimiter = "<<output-end>>";

static const wxString kScriptContent = R"#(#!/bin/bash
# Kennel remote helper — reads commands from stdin, writes output to stdout.
# Each response ends with the line: <<output-end>>
set -o pipefail

# The path comes as text, not through a shell: expand a leading $HOME or ~
# (a session folder is stored as "$HOME/...").
expand_home() {
    case "$1" in
        '$HOME'|'$HOME/'*) printf '%s' "$HOME${1#\$HOME}" ;;
        '~'|'~/'*) printf '%s' "$HOME${1#\~}" ;;
        *) printf '%s' "$1" ;;
    esac
}

do_list_files() {
    local dir
    dir=$(expand_home "${1:-.}")
    if [ ! -d "$dir" ]; then
        echo "error: not a directory: $dir"
        echo "<<output-end>>"
        return
    fi
    local resolved
    resolved=$(cd "$dir" && pwd -P 2>/dev/null || echo "$dir")
    echo "cwd:$resolved"
    find "$dir" -mindepth 1 -maxdepth 1 | while IFS= read -r item; do
        local name
        name=$(basename "$item")
        if [ -d "$item" ]; then
            echo "${name}|0|dir"
        else
            local size=0
            if stat -f %z "$item" >/dev/null 2>&1; then
                size=$(stat -f %z "$item")
            elif stat -c %s "$item" >/dev/null 2>&1; then
                size=$(stat -c %s "$item")
            fi
            echo "${name}|${size}|file"
        fi
    done
    echo "<<output-end>>"
}

# The commands that change files print "ok", or "error: <why>".
do_new_folder() {
    local p msg
    p=$(expand_home "$1")
    if [ -e "$p" ] || [ -L "$p" ]; then
        echo "error: $p already exists"
    elif msg=$(mkdir -- "$p" 2>&1); then
        echo "ok"
    else
        echo "error: $msg"
    fi
    echo "<<output-end>>"
}

do_new_file() {
    local p msg
    p=$(expand_home "$1")
    if [ -e "$p" ] || [ -L "$p" ]; then
        echo "error: $p already exists"
    elif msg=$( (set -C; : > "$p") 2>&1); then
        echo "ok"
    else
        echo "error: $msg"
    fi
    echo "<<output-end>>"
}

# Deletes a file, or a folder and all in it. A symbolic link is removed, never
# followed.
do_delete() {
    local p msg
    p=$(expand_home "$1")
    case "$p" in
        ''|/|"$HOME"|"$HOME/")
            echo "error: refusing to delete $p"
            echo "<<output-end>>"
            return
            ;;
    esac
    if [ ! -e "$p" ] && [ ! -L "$p" ]; then
        echo "error: $p does not exist"
    elif msg=$(rm -rf -- "$p" 2>&1); then
        echo "ok"
    else
        echo "error: $msg"
    fi
    echo "<<output-end>>"
}

do_find_cli() {
    local tools=(
        claude
        kiro-cli
        codex
        aider
        copilot
        cursor
        continue
        goose
        amp
    )

    local home="$HOME"
    local search_path="$PATH"
    local extra_dirs=(
        "$home/.local/bin"
        "$home/.cargo/bin"
        "$home/.nvm/current/bin"
        "$home/.volta/bin"
        "$home/.bun/bin"
        "$home/go/bin"
        "/usr/local/bin"
        "/opt/homebrew/bin"
        "/opt/homebrew/sbin"
        "/usr/local/go/bin"
        "/snap/bin"
    )
    for d in "${extra_dirs[@]}"; do
        if [ -d "$d" ] && [[ ":$search_path:" != *":$d:"* ]]; then
            search_path="$search_path:$d"
        fi
    done

    IFS=':' read -ra dirs <<< "$search_path"
    for tool in "${tools[@]}"; do
        for dir in "${dirs[@]}"; do
            local candidate="$dir/$tool"
            if [ -x "$candidate" ] && [ -f "$candidate" ]; then
                echo "${tool}|${candidate}"
                break
            fi
        done
    done
    echo "<<output-end>>"
}

while IFS= read -r line; do
    cmd="${line%% *}"
    arg="${line#* }"
    case "$cmd" in
        list-files) do_list_files "$arg" ;;
        find-cli) do_find_cli ;;
        new-folder) do_new_folder "$arg" ;;
        new-file) do_new_file "$arg" ;;
        delete) do_delete "$arg" ;;
        *) echo "error: unknown command: $cmd"; echo "<<output-end>>" ;;
    esac
done
)#";

KennelRemote::KennelRemote(const wxString &host, const wxString &user)
    : m_host{host}, m_user{user} {}

KennelRemote::~KennelRemote() {
  if (m_process && m_process->IsRunning()) {
    m_process->Stop();
  }
}

wxString KennelRemote::ConnectString() const {
  if (m_user.empty()) {
    return m_host;
  }
  return wxString::Format("%s@%s", m_user, m_host);
}

void KennelRemote::AppendOptions(std::vector<std::string> &argv) const {
  if (m_batch) {
    for (const char *arg : {"-o", "BatchMode=yes", "-o", "ConnectTimeout=5"}) {
      argv.push_back(arg);
    }
  }
}

bool KennelRemote::IsRunning() const {
  return m_process && m_process->IsRunning();
}

bool KennelRemote::EnsureReady() {
  if (IsRunning()) {
    return true;
  }
  // A worker thread must not touch the UI: it only logs.
  const bool onUiThread = wxIsMainThread();
  std::optional<wxBusyCursor> busy;
  if (onUiThread) {
    busy.emplace();
  }
  const Status st = Start();
  if (st.ok()) {
    return true;
  }
  if (onUiThread) {
    busy.reset();
    ::wxMessageBox(st.message(), "Kennel", wxICON_WARNING | wxOK);
  } else {
    KLOG_WARN() << st.message();
  }
  return false;
}

Status KennelRemote::Start() {
  if (IsRunning()) {
    return Status::Ok();
  }

  const long pid = static_cast<long>(wxGetProcessId());
  const wxString remoteTmpDir = wxString::Format("/tmp/kennel/%ld", pid);
  const wxString remoteScriptPath = remoteTmpDir + "/kennel-remote.sh";

  const wxString localScriptPath = wxFileName::GetTempDir() +
                                   wxFileName::GetPathSeparator() +
                                   "kennel-remote.sh";

  {
    // Hosts can connect at the same time, on worker threads, and they share
    // this file: it is written once, so that no scp reads it while another
    // host writes it.
    static std::mutex s_scriptMutex;
    static bool s_scriptWritten{false};
    std::lock_guard<std::mutex> lock(s_scriptMutex);
    if (!s_scriptWritten || !wxFileName::FileExists(localScriptPath)) {
      wxFFile fp(localScriptPath, "w+b");
      if (!fp.IsOpened() || !fp.Write(kScriptContent, wxConvUTF8) ||
          !fp.Close()) {
        m_scriptUploaded = false;
        return Status::Error(
            _("Failed to write kennel-remote.sh to temp dir."));
      }
      s_scriptWritten = true;
    }
  }

  {
    const std::string mkdirCmd =
        wxString::Format("mkdir -p %s", remoteTmpDir).ToStdString(wxConvUTF8);
    std::vector<std::string> mkArgv{"ssh"};
    AppendOptions(mkArgv);
    mkArgv.push_back(ConnectString().ToStdString(wxConvUTF8));
    mkArgv.push_back(mkdirCmd);
    auto mkResult = RunProcessWithTimeout(mkArgv, 5);
    if (!mkResult.ok()) {
      m_scriptUploaded = false;
      return Status::Error(wxString::Format(
          _("Failed to create remote directory %s:\n%s"), remoteTmpDir,
          wxString::FromUTF8(mkResult.status().message())));
    }
  }

  {
    std::vector<std::string> scpArgv{"scp"};
    AppendOptions(scpArgv);
    scpArgv.push_back(localScriptPath.ToStdString(wxConvUTF8));
    scpArgv.push_back(
        wxString::Format("%s:%s", ConnectString(), remoteScriptPath)
            .ToStdString(wxConvUTF8));
    auto scpResult = RunProcessWithTimeout(scpArgv, 10);
    if (!scpResult.ok()) {
      m_scriptUploaded = false;
      return Status::Error(
          wxString::Format(_("Failed to upload kennel-remote.sh:\n%s"),
                           wxString::FromUTF8(scpResult.status().message())));
    }
  }

  auto outputCb = [this](const std::string &out,
                         const std::string & /*err*/) -> bool {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputBuf += out;
    if (m_outputBuf.find(kOutputEndDelimiter.ToStdString(wxConvUTF8)) !=
        std::string::npos) {
      m_outputComplete = true;
      m_cv.notify_all();
    }
    return true;
  };

  std::vector<std::string> sshArgv{"ssh", "-o", "ServerAliveInterval=10"};
  AppendOptions(sshArgv);
  sshArgv.push_back("-tt");
  sshArgv.push_back(ConnectString().ToStdString(wxConvUTF8));
  sshArgv.push_back(
      wxString::Format("bash %s", remoteScriptPath).ToStdString(wxConvUTF8));
  m_process = Process::StartInteractive(sshArgv, outputCb);

  if (!m_process) {
    m_scriptUploaded = false;
    return Status::Error(_("Failed to start remote session."));
  }

  m_scriptUploaded = true;
  return Status::Ok();
}

std::optional<std::string> KennelRemote::SendCommand(const std::string &cmd,
                                                     int timeoutSecs) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputBuf.clear();
    m_outputComplete = false;
  }

  if (!m_process->WriteLine(cmd)) {
    return std::nullopt;
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  const bool got = m_cv.wait_for(lock, std::chrono::seconds(timeoutSecs),
                                 [this] { return m_outputComplete; });
  if (!got) {
    return std::nullopt;
  }

  const std::string delim = kOutputEndDelimiter.ToStdString(wxConvUTF8) + "\n";
  std::string result = m_outputBuf;
  const auto pos = result.rfind(delim);
  if (pos != std::string::npos) {
    result.erase(pos);
  }
  return result;
}

Status KennelRemote::RunPathCommand(const std::string &command,
                                    const wxString &path) {
  // One command is one line: a path with a line break can not be sent.
  if (path.empty() || path.find_first_of("\r\n") != wxString::npos) {
    return Status::Error(wxString::Format(_("Invalid path: %s"), path));
  }
  if (Status st = Start(); !st.ok()) {
    return st;
  }
  auto output = SendCommand(command + " " + path.ToStdString(wxConvUTF8), 30);
  if (!output) {
    return Status::Error(
        wxString::Format(_("%s did not answer in time"), ConnectString()));
  }
  auto lines =
      ::wxStringTokenize(wxString::FromUTF8(*output), "\r\n", wxTOKEN_STRTOK);
  wxString error;
  for (auto &line : lines) {
    line.Trim().Trim(false);
    if (line == "ok") {
      return Status::Ok();
    }
    if (line.StartsWith("error:", &error)) {
      return Status::Error(error.Trim(false));
    }
  }
  return Status::Error(
      wxString::Format(_("Unexpected answer from %s"), ConnectString()));
}

Status KennelRemote::MakeFolder(const wxString &path) {
  return RunPathCommand("new-folder", path);
}

Status KennelRemote::MakeFile(const wxString &path) {
  return RunPathCommand("new-file", path);
}

Status KennelRemote::Remove(const wxString &path) {
  return RunPathCommand("delete", path);
}

std::vector<KennelRemote::FileEntry>
KennelRemote::ListFiles(const wxString &dir, wxString *outCwd) {
  if (!EnsureReady()) {
    return {};
  }

  const std::string cmd = "list-files " + dir.ToStdString(wxConvUTF8);
  auto output = SendCommand(cmd, 10);
  if (!output) {
    return {};
  }

  std::vector<FileEntry> files;
  auto lines =
      ::wxStringTokenize(wxString::FromUTF8(*output), "\r\n", wxTOKEN_STRTOK);

  for (auto &line : lines) {
    line.Trim().Trim(false);
    if (line.empty()) {
      continue;
    }
    if (line.StartsWith("cwd:")) {
      if (outCwd) {
        *outCwd = line.Mid(4).Trim().Trim(false);
      }
      continue;
    }
    if (line.StartsWith("error:")) {
      KLOG_WARN() << "Remote error: " << line;
      continue;
    }

    auto parts = ::wxStringTokenize(line, "|", wxTOKEN_STRTOK);
    if (parts.size() != 3) {
      continue;
    }

    FileEntry f;
    f.name = parts[0];
    f.isDir = (parts[2] == "dir");
    if (!f.isDir) {
      unsigned long sz{0};
      parts[1].ToCULong(&sz);
      f.size = static_cast<size_t>(sz);
    }
    files.push_back(f);
  }
  return files;
}

std::unordered_map<wxString, std::vector<KennelRemote::FoundCli>>
    KennelRemote::s_cliCache;
std::mutex KennelRemote::s_cliCacheMutex;

std::vector<KennelRemote::FoundCli> KennelRemote::FindCli() {
  const wxString cacheKey = ConnectString();
  {
    std::lock_guard<std::mutex> lock(s_cliCacheMutex);
    if (const auto it = s_cliCache.find(cacheKey); it != s_cliCache.end()) {
      return it->second;
    }
  }

  if (!EnsureReady()) {
    return {};
  }

  auto output = SendCommand("find-cli", 15);
  if (!output) {
    return {};
  }

  std::vector<FoundCli> results;
  auto lines =
      ::wxStringTokenize(wxString::FromUTF8(*output), "\r\n", wxTOKEN_STRTOK);
  for (auto &line : lines) {
    line.Trim().Trim(false);
    if (line.empty() || line.StartsWith("error:")) {
      continue;
    }
    auto parts = ::wxStringTokenize(line, "|", wxTOKEN_STRTOK);
    if (parts.size() != 2) {
      continue;
    }
    results.push_back({parts[0], parts[1]});
  }

  {
    std::lock_guard<std::mutex> lock(s_cliCacheMutex);
    s_cliCache[cacheKey] = results;
  }
  return results;
}

void KennelRemote::ClearCliCache() {
  std::lock_guard<std::mutex> lock(s_cliCacheMutex);
  s_cliCache.clear();
}
