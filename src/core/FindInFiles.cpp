#include "core/FindInFiles.h"

#include "core/Logger.h"
#include "core/Process.hpp"
#include "core/Ripgrep.h"
#include "core/SshClient.h"
#include "core/json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <wx/dir.h>

wxDEFINE_EVENT(wxEVT_FIND_BATCH, FindEvent);
wxDEFINE_EVENT(wxEVT_FIND_DONE, FindEvent);

namespace {

using json = nlohmann::json;

constexpr size_t kBatchSize = 100;
constexpr auto kBatchInterval = std::chrono::milliseconds(100);
constexpr size_t kMaxTextLength = 400;
// Lines of rg's output that a remote search keeps (a match is 3 to 4 lines).
constexpr int kRemoteMaxLines = 60000;
constexpr size_t kMaxErrorText = 4096;

// The characters that mean something in a regular expression.
bool IsRegexMeta(char c) {
  switch (c) {
  case '\\':
  case '.':
  case '+':
  case '*':
  case '?':
  case '(':
  case ')':
  case '|':
  case '[':
  case ']':
  case '{':
  case '}':
  case '^':
  case '$':
  case '#':
  case '&':
  case '-':
  case '~':
    return true;
  default:
    return false;
  }
}

// The code point that starts at `text[i]` (UTF-8) and its length in bytes. A
// byte that is not valid UTF-8 counts as a code point of its own.
std::pair<uint32_t, size_t> NextCodePoint(const std::string &text, size_t i) {
  const auto byte = [&](size_t k) {
    return static_cast<unsigned char>(text[k]);
  };
  const unsigned char lead = byte(i);
  size_t length = 1;
  uint32_t code = lead;
  if (lead >= 0xF0) {
    length = 4;
    code = lead & 0x07;
  } else if (lead >= 0xE0) {
    length = 3;
    code = lead & 0x0F;
  } else if (lead >= 0xC0) {
    length = 2;
    code = lead & 0x1F;
  }
  if (i + length > text.size()) {
    return {lead, 1};
  }
  for (size_t k = 1; k < length; ++k) {
    if ((byte(i + k) & 0xC0) != 0x80) {
      return {lead, 1};
    }
    code = (code << 6) | (byte(i + k) & 0x3F);
  }
  return {code, length};
}

// The pattern as a regular expression made of ASCII only. The arguments of a
// program reach it in the code page of the system on Windows, so a letter like
// an accented letter would be lost; `\x{E9}` says the same. A pattern that is
// not a regular expression has its special characters escaped, so rg never has
// to be told it is plain text (-F). Works on the UTF-8 text, so that a
// character outside the BMP is one code point (a wxString of wchar_t holds it
// as two halves).
std::string PatternArgument(const FindQuery &query) {
  const std::string text = query.pattern.ToStdString(wxConvUTF8);
  std::string out;
  for (size_t i = 0; i < text.size();) {
    const auto [code, length] = NextCodePoint(text, i);
    if (code > 0x7f) {
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "\\x{%X}",
                    static_cast<unsigned>(code));
      out += buffer;
    } else {
      if (!query.isRegex && IsRegexMeta(static_cast<char>(code))) {
        out += '\\';
      }
      out += static_cast<char>(code);
    }
    i += length;
  }
  return out;
}

// The globs of "*.cpp, *.hpp" (a comma or a semicolon ends one). A comma inside
// braces belongs to the glob: "*.{cpp,hpp}" is one. When a brace is never
// closed (a typo) the braces are ignored, so that the commas still split.
std::vector<std::string> SplitGlobs(const wxString &text,
                                    bool keepBraces = true) {
  std::vector<std::string> globs;
  wxString current;
  int braces = 0;
  auto push = [&] {
    current.Trim().Trim(false);
    if (!current.empty()) {
      globs.push_back(current.ToStdString(wxConvUTF8));
    }
    current.clear();
  };
  for (wxUniChar c : text) {
    if (keepBraces) {
      if (c == '{') {
        ++braces;
      } else if (c == '}' && braces > 0) {
        --braces;
      }
    }
    if (braces == 0 && (c == ',' || c == ';')) {
      push();
    } else {
      current << c;
    }
  }
  if (braces != 0) {
    return SplitGlobs(text, /*keepBraces=*/false);
  }
  push();
  return globs;
}

// `text` for a POSIX shell, in single quotes.
std::string ShellQuote(const std::string &text) {
  std::string quoted = "'";
  for (char c : text) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  quoted += '\'';
  return quoted;
}

// The number of characters in the UTF-8 `text` from byte `from` up to byte
// `to`: the bytes that are not continuation bytes. Never fails on bad UTF-8.
int CountChars(const std::string &text, size_t from, size_t to) {
  to = std::min(to, text.size());
  int count = 0;
  for (size_t i = from; i < to; ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
      ++count;
    }
  }
  return count;
}

wxString Decode(const std::string &text) {
  wxString decoded = wxString::FromUTF8(text.data(), text.size());
  if (decoded.empty() && !text.empty()) {
    // Not UTF-8 (a file in a code page): show it byte by byte.
    decoded = wxString(text.data(), wxConvISO8859_1, text.size());
  }
  return decoded;
}

const json *Member(const json &object, const char *key) {
  if (!object.is_object()) {
    return nullptr;
  }
  auto it = object.find(key);
  return it == object.end() ? nullptr : &*it;
}

// The "text" of rg's {"text": "..."} / {"bytes": "<base64>"} values; null for
// bytes (a name or a line that is not UTF-8: skipped).
const std::string *TextOf(const json &object, const char *key) {
  const json *value = Member(object, key);
  if (value == nullptr) {
    return nullptr;
  }
  const json *text = Member(*value, "text");
  return text != nullptr && text->is_string()
             ? text->get_ptr<const std::string *>()
             : nullptr;
}

// Reads the output of `rg --json`, line by line, and sends the matches to the
// handler in batches.
class Collector {
public:
  Collector(wxEvtHandler *handler, int generation)
      : m_handler(handler), m_generation(generation),
        m_lastFlush(std::chrono::steady_clock::now()) {}

  // `chunk` is whatever rg wrote (may end in the middle of a line). False when
  // the search has enough and rg should stop.
  bool Feed(const std::string &chunk) {
    m_buffer += chunk;
    size_t start = 0;
    for (size_t end; (end = m_buffer.find('\n', start)) != std::string::npos;
         start = end + 1) {
      ParseLine(m_buffer.substr(start, end - start));
    }
    m_buffer.erase(0, start);
    if (m_total >= FindInFiles::kMaxMatches) {
      m_truncated = true;
    }
    MaybeFlush();
    return !m_truncated;
  }

  // Sends what is left.
  void Finish() {
    if (!m_buffer.empty()) {
      ParseLine(m_buffer);
      m_buffer.clear();
    }
    Flush();
  }

  size_t Total() const { return m_total; }
  bool IsTruncated() const { return m_truncated; }
  // Drops what was not sent yet (a cancelled search sends nothing more).
  void Discard() { m_batch.clear(); }

private:
  void ParseLine(const std::string &line) {
    if (line.empty() || m_total >= FindInFiles::kMaxMatches) {
      return;
    }
    const json message = json::parse(line, /*cb=*/nullptr,
                                     /*allow_exceptions=*/false);
    if (message.is_discarded()) {
      return;
    }
    const json *type = Member(message, "type");
    const json *data = Member(message, "data");
    if (type == nullptr || !type->is_string() || data == nullptr ||
        type->get_ref<const std::string &>() != "match") {
      return;
    }
    const std::string *path = TextOf(*data, "path");
    const std::string *lineText = TextOf(*data, "lines");
    const json *number = Member(*data, "line_number");
    if (path == nullptr || lineText == nullptr || number == nullptr ||
        !number->is_number_integer()) {
      return;
    }

    // The first match of the line.
    size_t matchStart = 0;
    size_t matchEnd = 0;
    if (const json *subs = Member(*data, "submatches");
        subs != nullptr && subs->is_array() && !subs->empty()) {
      const json &first = (*subs)[0];
      const json *s = Member(first, "start");
      const json *e = Member(first, "end");
      if (s != nullptr && e != nullptr && s->is_number_unsigned() &&
          e->is_number_unsigned()) {
        matchStart = s->get<size_t>();
        matchEnd = e->get<size_t>();
      }
    }

    // rg writes the whole line, however long (--max-columns does not apply to
    // --json), so a minified file costs a long line here: it is cut below.
    std::string raw = *lineText;
    while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r')) {
      raw.pop_back();
    }
    size_t indent = 0;
    while (indent < raw.size() && (raw[indent] == ' ' || raw[indent] == '\t')) {
      ++indent;
    }
    matchStart = std::min(matchStart, raw.size());
    matchEnd = std::clamp(matchEnd, matchStart, raw.size());

    FindMatch match;
    match.path = Decode(*path);
    match.line = number->get<int>();
    match.column = CountChars(raw, 0, matchStart) + 1;
    const std::string trimmed = raw.substr(indent);
    const size_t from = std::max(matchStart, indent) - indent;
    const size_t to = std::max(matchEnd, indent) - indent;
    match.text = Decode(trimmed);
    match.matchStart = CountChars(trimmed, 0, from);
    match.matchLength = CountChars(trimmed, from, to);
    if (match.text.length() > kMaxTextLength) {
      // Counts wchar_t (two for a character outside the BMP on Windows), while
      // matchStart and matchLength count characters: the clamp can be off by a
      // few for such text, which only moves the highlight.
      match.text = match.text.Left(kMaxTextLength);
      match.matchStart = std::min<int>(match.matchStart, kMaxTextLength);
      match.matchLength =
          std::min<int>(match.matchLength, kMaxTextLength - match.matchStart);
    }
    m_batch.push_back(std::move(match));
    ++m_total;
  }

  void MaybeFlush() {
    if (m_batch.size() >= kBatchSize ||
        (!m_batch.empty() &&
         std::chrono::steady_clock::now() - m_lastFlush >= kBatchInterval)) {
      Flush();
    }
  }

  void Flush() {
    m_lastFlush = std::chrono::steady_clock::now();
    if (m_batch.empty()) {
      return;
    }
    auto *event = new FindEvent(wxEVT_FIND_BATCH);
    event->SetInt(m_generation);
    event->Matches() = std::move(m_batch);
    m_batch.clear();
    m_handler->QueueEvent(event);
  }

  wxEvtHandler *m_handler;
  int m_generation;
  std::string m_buffer;
  std::vector<FindMatch> m_batch;
  size_t m_total{0};
  bool m_truncated{false};
  std::chrono::steady_clock::time_point m_lastFlush;
};

wxString TrimmedError(const std::string &text, const wxString &fallback) {
  wxString message = Decode(text);
  message.Trim().Trim(false);
  return message.empty() ? fallback : message;
}

} // namespace

std::vector<std::string> FindInFiles::BuildArguments(const FindQuery &query) {
  // --no-config: the RIPGREP_CONFIG_PATH of the user must not change the
  // output or what is searched. --no-messages: a file that can not be read
  // (permissions, a broken link) is not an error of the search; a bad regular
  // expression still is one.
  std::vector<std::string> args = {
      "--no-config", "--no-messages",
      "--json",      "--max-filesize",
      "10M",         query.matchCase ? "--case-sensitive" : "--ignore-case"};
  if (query.wholeWord) {
    args.push_back("--word-regexp");
  }
  for (const std::string &glob : SplitGlobs(query.include)) {
    args.push_back("--glob");
    args.push_back(glob);
  }
  for (const std::string &glob : SplitGlobs(query.exclude)) {
    args.push_back("--glob");
    args.push_back("!" + glob);
  }
  args.push_back("--regexp");
  args.push_back(PatternArgument(query));
  return args;
}

FindInFiles::FindInFiles(wxEvtHandler *handler) : m_handler(handler) {}

FindInFiles::~FindInFiles() {
  Cancel();
  if (m_future.valid()) {
    m_future.wait();
  }
}

bool FindInFiles::IsRunning() const {
  // The future is ready only after the worker has returned, which is after it
  // queued wxEVT_FIND_DONE: a handler of that event would still see a search
  // that runs. `m_reported` says it is over for the caller.
  return m_future.valid() && !m_reported;
}

void FindInFiles::Cancel() { m_cancel = true; }

Status FindInFiles::Start(const Account &account, const FindQuery &query) {
  if (IsRunning()) {
    return Status::Error(_("A search is already running"));
  }
  if (m_future.valid()) {
    // The worker of the last search sent its answer and is about to return (it
    // only has to free what it holds): this waits for that, not for a search.
    m_future.wait();
  }
  if (query.pattern.empty()) {
    return Status::Error(_("There is nothing to search for"));
  }
  if (query.root.empty()) {
    return Status::Error(_("There is no folder to search in"));
  }
  m_cancel = false;
  m_reported = false;
  const int generation = ++m_generation;
  m_future = std::async(std::launch::async, [this, account, query, generation] {
    Run(account, query, generation);
  });
  return Status::Ok();
}

void FindInFiles::Run(Account account, FindQuery query, int generation) {
  // WSL asks wsl.exe for the home folder the first time: not on the UI thread.
  const wxString exe = ripgrep::ExecutableOf(account);

  Collector collector(m_handler, generation);
  Status result;
  const std::vector<std::string> args = BuildArguments(query);
  const std::string root = query.root.ToStdString(wxConvUTF8);

  if (exe.empty()) {
    result = Status::Error(_("ripgrep is not installed"));
  } else if (account.IsLocalHost() && !wxDir::Exists(query.root)) {
    // rg says nothing about a missing folder with --no-messages.
    result = Status::Error(_("The folder does not exist"));
  } else if (account.IsRemote()) {
    // The exit code of rg is lost in the pipe to head, so what went wrong is
    // told by stderr: a folder that is not there, a regular expression that
    // does not parse, an rg that is not installed (from the shell).
    std::string script =
        "test -d " + ShellQuote(root) + " || { echo " +
        ShellQuote(_("The folder does not exist").ToStdString(wxConvUTF8)) +
        " >&2; exit 2; }; " + exe.ToStdString(wxConvUTF8);
    for (const std::string &arg : args) {
      script += " " + ShellQuote(arg);
    }
    script += " -- " + ShellQuote(root) + " | head -n " +
              std::to_string(kRemoteMaxLines);
    // The script is POSIX shell, and the shell of the user (fish, tcsh) may not
    // be one: it only has to read `sh -c` and one word in single quotes. $HOME
    // in the path of rg is expanded by sh.
    const std::string command = "sh -c " + ShellQuote(script);
    auto reply = SshClient::Execute(account.m_name, account.m_user,
                                    wxString::FromUTF8(command),
                                    std::chrono::minutes(5));
    if (!reply.ok()) {
      result = reply.status();
    } else if (!m_cancel) {
      collector.Feed(reply->out);
      if (collector.Total() == 0 && !reply->err.empty()) {
        result = Status::Error(TrimmedError(reply->err, _("ripgrep failed")));
      }
    }
  } else {
    std::vector<std::string> argv;
    if (account.IsWSL()) {
      argv = {"wsl.exe", "-d", account.m_name.ToStdString(wxConvUTF8),
              "--exec"};
    }
    argv.push_back(exe.ToStdString(wxConvUTF8));
    argv.insert(argv.end(), args.begin(), args.end());
    argv.push_back("--");
    argv.push_back(root);
    // On Windows the program gets one command line: the folder, a glob or the
    // pattern with a space would fall apart.
    for (std::string &arg : argv) {
      arg = Process::QuoteArgument(arg);
    }

    std::string errorText;
    const int code = Process::RunProcessAndWait(
        argv, [&](const std::string &out, const std::string &err) {
          if (errorText.size() < kMaxErrorText) {
            errorText += err;
          }
          if (m_cancel) {
            return false;
          }
          return collector.Feed(out);
        });
    // 0: matches, 1: none, 2: an error (also when only some files could not be
    // read, which is not worth a message when there are matches). rg that we
    // stopped ends with another code.
    if (!m_cancel && !collector.IsTruncated()) {
      if (code < 0) {
        result = Status::Error(
            TrimmedError(errorText, _("ripgrep could not be started")));
      } else if (code > 1 && collector.Total() == 0) {
        // --no-messages: rg says nothing when the folder is not there (WSL has
        // no check of its own), and exits with 2.
        result = Status::Error(TrimmedError(
            errorText,
            code == 2
                ? wxString(_("The folder does not exist or "
                             "can not be searched"))
                : wxString::Format(_("ripgrep failed (exit code %d)"), code)));
      }
    }
  }

  if (m_cancel) {
    collector.Discard();
  } else {
    collector.Finish();
  }
  if (!result.ok()) {
    KLOG_WARN() << "Find in files: " << result.message();
  }

  auto *done = new FindEvent(wxEVT_FIND_DONE);
  done->SetInt(generation);
  done->SetResult(result);
  done->SetTruncated(collector.IsTruncated());
  done->SetCancelled(m_cancel);
  // Before the event, so that its handler can start another search.
  m_reported = true;
  m_handler->QueueEvent(done);
}
