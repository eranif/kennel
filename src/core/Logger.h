#pragma once

#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/string.h>

#include <sstream>
#include <vector>

// Severity levels, ordered ascending. A message is emitted only when its level
// is >= the logger's configured threshold.
enum class LogLevel {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
};

// File-backed logger with an ostream-style streaming API, modeled on the
// wxTerminalEmulator logger. Use the KLOG_* macros:
//
//     KLOG_INFO() << "loaded " << count << " adapters from " << path;
//
// A trailing newline is appended automatically. The logger is a process-wide
// singleton; call SetLogFile() once at startup (after AppPaths bootstrap).
// It never throws and never uses the wxLog* mechanism.
class Logger {
public:
  static Logger &Get();

  void SetLevel(LogLevel level) { m_level = level; }
  LogLevel GetLevel() const { return m_level; }

  // Directs output to the given file (appending). Reopens if already open.
  void SetLogFile(const wxString &path);

  // One streamed log message. Accumulates via operator<< and flushes to the
  // logger on destruction (only if its level passed the threshold).
  class LogEntry {
  public:
    LogEntry(LogLevel level, bool enabled);
    ~LogEntry();

    LogEntry(const LogEntry &) = delete;
    LogEntry &operator=(const LogEntry &) = delete;

    // Anything std::ostream supports (numbers, std::hex, etc.).
    template <typename T> LogEntry &operator<<(const T &v) {
      if (m_enabled)
        m_ss << v;
      return *this;
    }

    // Stream manipulators (std::hex, std::dec, std::endl, ...).
    LogEntry &operator<<(std::ostream &(*manip)(std::ostream &)) {
      if (m_enabled)
        manip(m_ss);
      return *this;
    }

    // wx-specific overloads.
    LogEntry &operator<<(const wxString &s);
    LogEntry &operator<<(const wxFileName &fn);
    LogEntry &operator<<(const std::vector<wxString> &arr);

  private:
    std::ostringstream m_ss;
    LogLevel m_level;
    bool m_enabled;
  };

  LogEntry Log(LogLevel level);

private:
  Logger() = default;
  void Write(LogLevel level, const wxString &msg);
  void EnsureOpen();

  LogLevel m_level{LogLevel::kInfo};
  wxString m_logPath;
  wxFFile m_file;
};

#define KLOG(level) Logger::Get().Log(level)
#define KLOG_TRACE() KLOG(LogLevel::kTrace)
#define KLOG_DEBUG() KLOG(LogLevel::kDebug)
#define KLOG_INFO() KLOG(LogLevel::kInfo)
#define KLOG_WARN() KLOG(LogLevel::kWarn)
#define KLOG_ERROR() KLOG(LogLevel::kError)
