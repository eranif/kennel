#include "core/AwaitingInputDetector.h"
#include "Logger.h"

namespace {

#define BUFF_STATE_NORMAL 0
#define BUFF_STATE_IN_ESC 1
#define BUFF_STATE_IN_OSC 2

// see : https://en.wikipedia.org/wiki/ANSI_escape_code#Escape_sequences
std::string StripTerminalColouring(const std::string &buffer) {
  std::string modbuffer;
  modbuffer.reserve(buffer.length());
  short state = BUFF_STATE_NORMAL;
  for (const char &ch : buffer) {
    switch (state) {
    case BUFF_STATE_NORMAL:
      if (ch == 0x1B) { // found ESC char
        state = BUFF_STATE_IN_ESC;
      } else {
        modbuffer += ch;
      }
      break;
    case BUFF_STATE_IN_ESC:
      switch (ch) {
      case 'm':
      case 'K':
      case 'G':
      case 'J':
      case 'H':
      case 'X':
      case 'B':
      case 'C':
      case 'D':
      case 'd':
        state = BUFF_STATE_NORMAL;
        break;
      case ']':
        // Operating System Command
        state = BUFF_STATE_IN_OSC;
        break;
      default:
        break;
      }
      break;
    case BUFF_STATE_IN_OSC:
      if (ch == '\a') {
        // bell, leave the current state
        state = BUFF_STATE_NORMAL;
      }
      break;
    }
  }
  modbuffer.shrink_to_fit();
  return modbuffer;
}

constexpr size_t kMinLineLength = 5;
} // namespace

AwaitingInputDetector::AwaitingInputDetector(
    const std::vector<wxString> &patterns, NotifyFn onAwaiting,
    size_t maxBufferBytes)
    : m_onAwaiting(std::move(onAwaiting)), m_maxBufferBytes(maxBufferBytes),
      m_minMatchLen(kMinLineLength) {
  for (const wxString &pat : patterns) {
    auto re = std::make_unique<wxRegEx>(pat, wxRE_ADVANCED);
    if (!re->IsValid()) {
      KLOG_WARN() << "AwaitingInputDetector: skipping invalid pattern: " << pat;
      continue;
    }
    m_patterns.push_back({pat, std::move(re)});
  }
  if (m_patterns.empty()) {
    m_minMatchLen = 0; // nothing to match => never skip on length
  }
}

void AwaitingInputDetector::OnOutput(const std::string &chunk) {
  // Decode raw bytes as UTF-8 and accumulate. Match only on complete lines.
  m_buffer += wxString::FromUTF8(chunk);

  size_t nl;
  while ((nl = m_buffer.find('\n')) != wxString::npos) {
    wxString line = m_buffer.Left(nl);
    std::string c_line = line.ToStdString(wxConvUTF8);
    c_line = StripTerminalColouring(c_line);
    line = wxString::FromUTF8(c_line);
    m_buffer = m_buffer.Mid(nl + 1); // drop the checked line (and the '\n')
    line.Trim().Trim(false);
    if (line.empty()) {
      continue;
    }
    if (CheckLine(line)) {
      return; // fired; stop scanning until Reset()
    }
  }

  // Guard against an unbounded buffer if a stream never emits a newline.
  if (m_buffer.length() > m_maxBufferBytes) {
    m_buffer = m_buffer.Right(m_maxBufferBytes);
  }
}

bool AwaitingInputDetector::CheckLine(const wxString &line) {
  // A line shorter than the shortest possible match can't match any pattern.
  if (line.length() < m_minMatchLen) {
    return false;
  }
  for (const CompiledPattern &p : m_patterns) {
    if (p.re->Matches(line)) {
      if (m_onAwaiting) {
        m_onAwaiting();
      }
      return true;
    }
  }
  return false;
}

void AwaitingInputDetector::Reset() {
  // Only re-arm the match flag. The buffer is NOT cleared: complete lines are
  // already dropped as they are checked, so it only ever holds an unchecked
  // trailing partial line — which may be the start of the next prompt. Clearing
  // it here would drop the leading characters of a back-to-back prompt.
}
