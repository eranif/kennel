#include "core/TmuxCommands.h"

#include "core/ShellQuote.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <wx/wxcrt.h>

namespace tmux {

namespace {

std::string Utf8(const wxString &text) { return text.ToStdString(wxConvUTF8); }
wxString FromUtf8(const std::string &text) { return wxString::FromUTF8(text); }

bool IsAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)); }

// A word that a shell passes on as it is, without quotes. Short on purpose:
// `=` (zsh expands `=word` to a path) and `%` (a job) are quoted.
std::string ShellWord(const std::string &word) {
  if (word.empty()) {
    return ShellQuote(word);
  }
  for (const char c : word) {
    if (!IsAlnum(c) && std::strchr("_.,/:@+-", c) == nullptr) {
      return ShellQuote(word);
    }
  }
  return word;
}

// The target of a command that names a session: `=` makes the name match
// exactly, and not as the start of a longer name.
std::string ExactTarget(const wxString &name) {
  return ShellQuote("=" + Utf8(name));
}

std::string SocketArguments(Server server) {
  return "tmux -L " + ShellWord(Utf8(SocketOf(server)));
}

} // namespace

wxString SocketOf(Server server) {
  return server == Server::kAgent ? "kennel" : "kennel-term";
}

std::optional<Version> ParseVersion(const wxString &output) {
  const std::string text = Utf8(output);
  if (text.find("master") != std::string::npos) {
    return Version{999, 0};
  }
  size_t i = 0;
  while (i < text.size() &&
         !std::isdigit(static_cast<unsigned char>(text[i]))) {
    ++i;
  }
  if (i == text.size()) {
    return std::nullopt;
  }
  auto number = [&text](size_t &at) {
    int value = 0;
    while (at < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[at]))) {
      value = value * 10 + (text[at] - '0');
      ++at;
    }
    return value;
  };
  Version version;
  version.major = number(i);
  if (i < text.size() && text[i] == '.') {
    ++i;
    version.minor = number(i);
  }
  return version;
}

wxString DefaultAgentSettings() {
  return "set -g mouse on\n"
         "setw -g mode-keys vi\n"
         "set -g set-clipboard on\n"
         "set -g allow-passthrough all";
}

wxString DefaultTerminalSettings() {
  return "set -g mouse on\n"
         "setw -g mode-keys vi\n"
         "set -g set-clipboard on";
}

std::vector<Command> ParseCommands(const wxString &text) {
  const std::string in = Utf8(text);
  const size_t size = in.size();
  std::vector<Command> commands;
  Command words;
  std::string word;
  bool inWord = false;

  auto endWord = [&] {
    if (inWord) {
      words.push_back(FromUtf8(word));
      word.clear();
      inWord = false;
    }
  };
  auto endCommand = [&] {
    endWord();
    if (!words.empty()) {
      commands.push_back(std::move(words));
      words.clear();
    }
  };

  for (size_t i = 0; i < size; ++i) {
    const char c = in[i];
    if (c == '\n' || c == ';') {
      endCommand();
    } else if (c == '#' && !inWord && words.empty()) {
      while (i + 1 < size && in[i + 1] != '\n') {
        ++i; // A comment runs to the end of the line
      }
    } else if (c == ' ' || c == '\t' || c == '\r') {
      endWord();
    } else if (c == '\'') {
      inWord = true;
      for (++i; i < size && in[i] != '\''; ++i) {
        word += in[i];
      }
    } else if (c == '"') {
      inWord = true;
      for (++i; i < size && in[i] != '"'; ++i) {
        if (in[i] == '\\' && i + 1 < size) {
          ++i;
        }
        word += in[i];
      }
    } else if (c == '\\' && i + 1 < size) {
      inWord = true;
      word += in[++i];
    } else {
      inWord = true;
      word += c;
    }
  }
  endCommand();
  return commands;
}

std::vector<Command> AdaptToVersion(const std::vector<Command> &commands,
                                    const Version &version) {
  std::vector<Command> adapted;
  for (const Command &command : commands) {
    bool passthrough = false;
    for (const wxString &word : command) {
      if (word == "allow-passthrough") {
        passthrough = true;
      }
    }
    if (!passthrough) {
      adapted.push_back(command);
      continue;
    }
    if (!version.IsAtLeast(3, 3)) {
      continue; // tmux refuses the option
    }
    Command copy = command;
    if (!version.IsAtLeast(3, 4) && copy.back() == "all") {
      copy.back() = "on"; // `all` is new in 3.4
    }
    adapted.push_back(std::move(copy));
  }
  return adapted;
}

namespace {

// `text` for a part of a tmux session name: see SessionNameFor().
wxString NormaliseName(const wxString &text) {
  wxString result;
  bool lastWasDash = true; // So that there is no `-` at the start
  for (const wxUniChar c : text) {
    const bool replaced = c.GetValue() < 0x20 || c.GetValue() == 0x7f ||
                          wxIsspace(c) || c == '.' || c == ':' || c == '-';
    if (replaced) {
      if (!lastWasDash) {
        result += '-';
        lastWasDash = true;
      }
      continue;
    }
    result += c;
    lastWasDash = false;
  }
  if (result.EndsWith("-")) {
    result.RemoveLast();
  }
  return result;
}

} // namespace

wxString SessionNameFor(const wxString &group, const wxString &session) {
  wxString name = "kennel";
  for (const wxString &part : {NormaliseName(group), NormaliseName(session)}) {
    if (!part.empty()) {
      name << '-' << part;
    }
  }
  return name;
}

wxString AttachOrCreate(const LaunchSpec &spec) {
  std::string line = "env -u TMUX " + SocketArguments(spec.server) +
                     " -f /dev/null new-session -A -s " +
                     ShellWord(Utf8(spec.sessionName));
  if (!spec.command.empty()) {
    line += " sh -c " + ShellQuote(Utf8(spec.command));
  }
  // What must hold before the first line is drawn.
  line += " \\; set-option -g status off"
          " \\; set-option -g set-titles on"
          " \\; set-option -g window-size latest";
  for (const Command &command : spec.settings) {
    line += " \\;";
    for (const wxString &word : command) {
      line += " " + ShellWord(Utf8(word));
    }
  }
  return FromUtf8(line);
}

wxString ShimPrefix(const wxString &shimPath) {
  // In double quotes, so that `$HOME` is expanded by the shell that runs it.
  return "\"" + shimPath + "\" --";
}

wxString TerminalCommand(const wxString &shimPath) {
  const wxString shell = "\"${SHELL:-/bin/sh}\" -l";
  return "if [ -x \"" + shimPath + "\" ]; then " + ShimPrefix(shimPath) + " " +
         shell + "; else " + shell + "; fi";
}

wxString HasSession(Server server, const wxString &name) {
  return FromUtf8(SocketArguments(server) + " has-session -t " +
                  ExactTarget(name));
}

wxString KillSession(Server server, const wxString &name) {
  return FromUtf8(SocketArguments(server) + " kill-session -t " +
                  ExactTarget(name));
}

wxString ListSessions(Server server) {
  return FromUtf8(SocketArguments(server) +
                  " list-sessions -F "
                  "'#{session_name}|#{session_created}|#{session_attached}'");
}

wxString VersionProbe() { return "tmux -V"; }

std::vector<SessionInfo> ParseSessions(const wxString &output) {
  std::vector<SessionInfo> sessions;
  const std::string text = Utf8(output);
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = text.substr(start, end - start);
    start = end + 1;
    while (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    // From the right: a name may hold a `|`, the two numbers never do.
    const size_t last = line.rfind('|');
    const size_t middle = last == std::string::npos || last == 0
                              ? std::string::npos
                              : line.rfind('|', last - 1);
    if (middle == std::string::npos) {
      continue;
    }
    char *stop = nullptr;
    const std::string created = line.substr(middle + 1, last - middle - 1);
    const std::string attached = line.substr(last + 1);
    const long long createdValue = std::strtoll(created.c_str(), &stop, 10);
    if (created.empty() || *stop != '\0') {
      continue;
    }
    const long attachedValue = std::strtol(attached.c_str(), &stop, 10);
    if (attached.empty() || *stop != '\0') {
      continue;
    }
    SessionInfo info;
    info.name = FromUtf8(line.substr(0, middle));
    info.created = createdValue;
    info.attached = static_cast<int>(attachedValue);
    sessions.push_back(std::move(info));
  }
  return sessions;
}

} // namespace tmux
