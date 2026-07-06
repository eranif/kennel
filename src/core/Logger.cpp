#include "core/Logger.h"

#include <wx/datetime.h>

namespace {
wxString LevelToString(LogLevel level) {
  switch (level) {
  case LogLevel::kTrace:
    return "TRACE";
  case LogLevel::kDebug:
    return "DEBUG";
  case LogLevel::kInfo:
    return "INFO";
  case LogLevel::kWarn:
    return "WARN";
  case LogLevel::kError:
    return "ERROR";
  }
  return "?";
}
} // namespace

Logger &Logger::Get() {
  static Logger instance;
  return instance;
}

void Logger::SetLogFile(const wxString &path) {
  if (m_file.IsOpened()) {
    m_file.Close();
  }
  m_logPath = path;
  m_file.Open(m_logPath, "a");
}

void Logger::EnsureOpen() {
  if (m_file.IsOpened() || m_logPath.empty()) {
    return;
  }
  m_file.Open(m_logPath, "a");
}

void Logger::Write(LogLevel level, const wxString &msg) {
  EnsureOpen();
  if (!m_file.IsOpened()) {
    return;
  }
  wxDateTime now = wxDateTime::UNow();
  wxString line = now.FormatISOCombined(' ') +
                  wxString::Format(".%03ld", now.GetMillisecond()) + " [" +
                  LevelToString(level) + "] " + msg + "\n";
  m_file.Write(line);
  m_file.Flush();
}

Logger::LogEntry Logger::Log(LogLevel level) {
  return LogEntry(level, level >= m_level);
}

Logger::LogEntry::LogEntry(LogLevel level, bool enabled)
    : m_level(level), m_enabled(enabled) {}

Logger::LogEntry::~LogEntry() {
  if (m_enabled) {
    std::string s = m_ss.str();
    if (!s.empty()) {
      Logger::Get().Write(m_level, wxString::FromUTF8(s));
    }
  }
}

Logger::LogEntry &Logger::LogEntry::operator<<(const wxString &s) {
  if (m_enabled)
    m_ss << s.ToStdString(wxConvUTF8);
  return *this;
}

Logger::LogEntry &Logger::LogEntry::operator<<(const wxFileName &fn) {
  if (m_enabled)
    m_ss << fn.GetFullPath().ToStdString(wxConvUTF8);
  return *this;
}

Logger::LogEntry &
Logger::LogEntry::operator<<(const std::vector<wxString> &arr) {
  if (m_enabled) {
    m_ss << "[";
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0)
        m_ss << ", ";
      m_ss << arr[i].ToStdString(wxConvUTF8);
    }
    m_ss << "]";
  }
  return *this;
}
