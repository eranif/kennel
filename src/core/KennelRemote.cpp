#include "core/KennelRemote.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <chrono>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

static const wxString kOutputEndDelimiter = "<<output-end>>";

static const wxString kScriptContent = R"#(#!/bin/bash
# Kennel remote helper — reads commands from stdin, writes output to stdout.
# Each response ends with the line: <<output-end>>
set -o pipefail

do_list_files() {
    local dir="${1:-.}"
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

bool KennelRemote::IsRunning() const {
  return m_process && m_process->IsRunning();
}

bool KennelRemote::EnsureReady() {
  if (m_process && m_process->IsRunning()) {
    return true;
  }

  const long pid = static_cast<long>(wxGetProcessId());
  const wxString remoteTmpDir = wxString::Format("/tmp/kennel/%ld", pid);
  const wxString remoteScriptPath = remoteTmpDir + "/kennel-remote.sh";

  const wxString localScriptPath = wxFileName::GetTempDir() +
                                   wxFileName::GetPathSeparator() +
                                   "kennel-remote.sh";

  {
    wxFFile fp(localScriptPath, "w+b");
    if (!fp.IsOpened() || !fp.Write(kScriptContent, wxConvUTF8) ||
        !fp.Close()) {
      ::wxMessageBox(_("Failed to write kennel-remote.sh to temp dir."),
                     "Kennel", wxICON_WARNING | wxOK);
      m_scriptUploaded = false;
      return false;
    }
  }

  wxBusyCursor bc{};
  {
    const std::string mkdirCmd =
        wxString::Format("mkdir -p %s", remoteTmpDir).ToStdString(wxConvUTF8);
    auto mkResult = RunProcessWithTimeout(
        {"ssh", ConnectString().ToStdString(wxConvUTF8), mkdirCmd}, 5);
    if (!mkResult.ok()) {
      ::wxMessageBox(
          wxString::Format(_("Failed to create remote directory %s:\n%s"),
                           remoteTmpDir,
                           wxString::FromUTF8(mkResult.status().message())),
          "Kennel", wxICON_WARNING | wxOK);
      m_scriptUploaded = false;
      return false;
    }
  }

  {
    auto scpResult = RunProcessWithTimeout(
        {"scp", localScriptPath.ToStdString(wxConvUTF8),
         wxString::Format("%s:%s", ConnectString(), remoteScriptPath)
             .ToStdString(wxConvUTF8)},
        10);
    if (!scpResult.ok()) {
      ::wxMessageBox(
          wxString::Format(_("Failed to upload kennel-remote.sh:\n%s"),
                           wxString::FromUTF8(scpResult.status().message())),
          "Kennel", wxICON_WARNING | wxOK);
      m_scriptUploaded = false;
      return false;
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

  m_process = Process::StartInteractive(
      {"ssh", "-o", "ServerAliveInterval=10", "-tt",
       ConnectString().ToStdString(wxConvUTF8),
       wxString::Format("bash %s", remoteScriptPath).ToStdString(wxConvUTF8)},
      outputCb);

  if (!m_process) {
    ::wxMessageBox(_("Failed to start remote session."), "Kennel",
                   wxICON_WARNING | wxOK);
    m_scriptUploaded = false;
    return false;
  }

  m_scriptUploaded = true;
  return true;
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

std::vector<KennelRemote::FoundCli> KennelRemote::FindCli() {
  const wxString cacheKey = ConnectString();
  if (s_cliCache.count(cacheKey)) {
    return s_cliCache[cacheKey];
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

  s_cliCache[cacheKey] = results;
  return results;
}

void KennelRemote::ClearCliCache() { s_cliCache.clear(); }
