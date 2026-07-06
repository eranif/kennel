#pragma once

#include <wx/regex.h>
#include <wx/string.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

// Detects when a client is awaiting user input (e.g. a permission prompt) by
// matching adapter-defined regex patterns against the terminal's output. It is
// wired straight into wxTerminalViewCtrl::SetOutputCallback: OnOutput() has the
// same signature as the emulator's OutputCallback, so the terminal calls it
// with every raw output chunk.
//
// Matching is line-oriented: incoming bytes are decoded as UTF-8 and buffered;
// only *complete* lines (terminated by '\n') are tested, and each line is
// dropped once checked. This keeps matching cheap and avoids re-scanning the
// same text. On the first match it invokes the notify callback once; Reset()
// re-arms it.
class AwaitingInputDetector {
public:
  // Invoked once when output starts awaiting input (transition into the state).
  using NotifyFn = std::function<void()>;

  // Safety cap (bytes) on the line buffer so a stream that never emits a
  // newline cannot grow without bound.
  static constexpr size_t kDefaultMaxBufferBytes = 64 * 1024;

  // Patterns are raw regex strings (compiled here as wxRE_ADVANCED); invalid
  // ones are skipped. nullptr/empty onAwaiting is allowed.
  AwaitingInputDetector(const std::vector<wxString> &patterns,
                        NotifyFn onAwaiting,
                        size_t maxBufferBytes = kDefaultMaxBufferBytes);

  // Plug this into the terminal: term.SetOutputCallback([&](const std::string&
  // c){ detector.OnOutput(c); }). Appends the (UTF-8) chunk to the buffer and
  // tests each newly-completed line, firing the notify callback the first time
  // a line matches.
  void OnOutput(const std::string &chunk);

  // Clears the matched state and line buffer. Call when the user provides input
  // or the client resumes producing normal output, so the next prompt re-fires.
  void Reset();

  bool IsAwaiting() const { return m_matched; }

private:
  // A compiled pattern plus its source text (kept for logging).
  struct CompiledPattern {
    wxString source;
    std::unique_ptr<wxRegEx> re;
  };

  // Tests a single complete line against all patterns; fires + returns true on
  // the first match.
  bool CheckLine(const wxString &line);

  std::vector<CompiledPattern> m_patterns;
  NotifyFn m_onAwaiting;
  size_t m_maxBufferBytes;
  wxString m_buffer; // accumulates bytes until full lines can be extracted
  bool m_matched = false;
  // Shortest match length across all patterns: a line shorter than this cannot
  // match any pattern, so CheckLine skips it. SIZE_MAX with no patterns.
  size_t m_minMatchLen = static_cast<size_t>(-1);
};
