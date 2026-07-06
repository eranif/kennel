#include "core/SshConfigParser.h"

#include <wx/ffile.h>
#include <wx/tokenzr.h>

SshConfigParser SshConfigParser::Default() {
  wxFileName path = wxFileName::DirName(wxFileName::GetHomeDir());
  path.AppendDir(".ssh");
  path.SetFullName("config");
  return SshConfigParser(path);
}

SshConfigParser::SshConfigParser(const wxFileName &path) : m_path(path) {}

StatusOr<std::vector<SshHostEntry>> SshConfigParser::Parse() const {
  if (!m_path.FileExists()) {
    return std::vector<SshHostEntry>{};
  }

  wxFFile file(m_path.GetFullPath(), "r");
  if (!file.IsOpened()) {
    return Status::Error(
        wxString::Format("Cannot open SSH config: %s", m_path.GetFullPath()));
  }

  wxString content;
  if (!file.ReadAll(&content)) {
    return Status::Error(
        wxString::Format("Cannot read SSH config: %s", m_path.GetFullPath()));
  }

  std::vector<SshHostEntry> entries;
  wxString currentHost;

  wxStringTokenizer lines(content, "\n", wxTOKEN_RET_EMPTY);
  while (lines.HasMoreTokens()) {
    wxString line =
        lines.GetNextToken().Trim(false); // strip leading whitespace

    if (line.IsEmpty() || line.StartsWith("#")) {
      continue;
    }

    // Split into keyword and value on the first whitespace run.
    int sep = line.Find(' ');
    if (sep == wxNOT_FOUND) {
      sep = line.Find('\t');
    }
    if (sep == wxNOT_FOUND) {
      continue;
    }

    wxString keyword = line.Left(sep).Lower();
    wxString value = line.Mid(sep + 1).Trim(false).Trim(true);

    if (keyword == "host") {
      // A Host line may list multiple patterns separated by spaces; we only
      // handle single-name entries and skip wildcards.
      if (!value.Contains(' ') && !value.Contains('*') &&
          !value.Contains('?')) {
        currentHost = value;
      } else {
        currentHost.clear();
      }
    } else if (keyword == "hostname" && !currentHost.IsEmpty()) {
      entries.push_back({currentHost, value});
      currentHost.clear(); // consume: one HostName per Host block
    }
  }

  return entries;
}
