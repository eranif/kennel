#include "core/GitParser.h"

#include <wx/strconv.h>

#include <cstdlib>
#include <vector>

namespace git {
namespace {

wxString FromUtf8(const std::string &text) {
  return wxString::FromUTF8(text.data(), text.size());
}

// Splits `text` at `sep`. The empty pieces stay.
std::vector<std::string> Split(const std::string &text, char sep) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find(sep, start);
    if (end == std::string::npos) {
      parts.push_back(text.substr(start));
      break;
    }
    parts.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

// The first `count` fields of `line` (separated by one space) go to `fields`;
// what is left, spaces included, is the last element (a path can have spaces).
std::vector<std::string> SplitFields(const std::string &line, size_t count) {
  std::vector<std::string> fields;
  size_t start = 0;
  while (fields.size() < count) {
    const size_t end = line.find(' ', start);
    if (end == std::string::npos) {
      return {}; // Too short
    }
    fields.push_back(line.substr(start, end - start));
    start = end + 1;
  }
  fields.push_back(line.substr(start));
  return fields;
}

void ParseHeader(const std::string &line, GitStatus *status) {
  // "# branch.oid <commit>|(initial)", "# branch.head <name>|(detached)",
  // "# branch.upstream <name>", "# branch.ab +<ahead> -<behind>"
  const auto fields = SplitFields(line, 2);
  if (fields.size() != 3) {
    return;
  }
  const std::string &key = fields[1];
  const std::string &value = fields[2];
  if (key == "branch.oid") {
    status->oid = value == "(initial)" ? wxString() : FromUtf8(value);
  } else if (key == "branch.head") {
    status->detached = value == "(detached)";
    status->branch = status->detached ? wxString() : FromUtf8(value);
  } else if (key == "branch.upstream") {
    status->upstream = FromUtf8(value);
  } else if (key == "branch.ab") {
    const auto ab = Split(value, ' ');
    if (ab.size() == 2 && ab[0].size() > 1 && ab[1].size() > 1) {
      status->ahead = std::atoi(ab[0].c_str() + 1);
      status->behind = std::atoi(ab[1].c_str() + 1);
    }
  }
}

} // namespace

GitStatus ParseStatus(const std::string &output) {
  GitStatus status;
  const auto records = Split(output, '\0');
  for (size_t i = 0; i < records.size(); ++i) {
    const std::string &line = records[i];
    if (line.size() < 3) {
      continue;
    }
    GitChange change;
    switch (line[0]) {
    case '#':
      ParseHeader(line, &status);
      continue;
    case '?': // "? <path>"
      change.untracked = true;
      change.path = FromUtf8(line.substr(2));
      break;
    case '1': { // "1 XY sub mH mI mW hH hI <path>"
      const auto f = SplitFields(line, 8);
      if (f.size() != 9 || f[1].size() != 2) {
        continue;
      }
      change.index = f[1][0];
      change.worktree = f[1][1];
      change.path = FromUtf8(f[8]);
      break;
    }
    case '2': { // "2 XY sub mH mI mW hH hI Xscore <path>", then "<origPath>"
      const auto f = SplitFields(line, 9);
      if (f.size() != 10 || f[1].size() != 2) {
        continue;
      }
      change.index = f[1][0];
      change.worktree = f[1][1];
      change.path = FromUtf8(f[9]);
      if (i + 1 < records.size()) {
        change.origPath = FromUtf8(records[++i]);
      }
      break;
    }
    case 'u': { // "u XY sub m1 m2 m3 mW h1 h2 h3 <path>"
      const auto f = SplitFields(line, 10);
      if (f.size() != 11 || f[1].size() != 2) {
        continue;
      }
      change.index = f[1][0];
      change.worktree = f[1][1];
      change.unmerged = true;
      change.path = FromUtf8(f[10]);
      break;
    }
    default: // "!" (ignored) or something new
      continue;
    }
    status.changes.push_back(std::move(change));
  }
  return status;
}

} // namespace git
