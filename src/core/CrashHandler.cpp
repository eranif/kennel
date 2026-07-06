#include "core/CrashHandler.h"

#if defined(__unix__) || defined(__APPLE__)

#include <cstdio>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

namespace {

// Fixed-size path buffer written once by Install(); read-only in the handler.
char g_logDir[4096] = {};

// Async-signal-safe integer → decimal string writer into a fixed buffer.
// Returns number of bytes written (no NUL terminator added).
int WriteInt(char *buf, int bufSize, long long v) {
  if (bufSize <= 0)
    return 0;
  if (v == 0) {
    buf[0] = '0';
    return 1;
  }
  char tmp[32];
  int n = 0;
  bool neg = v < 0;
  unsigned long long u = neg ? static_cast<unsigned long long>(-v)
                             : static_cast<unsigned long long>(v);
  while (u > 0 && n < (int)sizeof(tmp)) {
    tmp[n++] = '0' + static_cast<int>(u % 10);
    u /= 10;
  }
  if (neg && n < bufSize) {
    buf[0] = '-';
    for (int i = 0; i < n; ++i)
      buf[i + 1] = tmp[n - 1 - i];
    return n + 1;
  }
  for (int i = 0; i < n && i < bufSize; ++i)
    buf[i] = tmp[n - 1 - i];
  return n;
}

// Write a NUL-terminated string to fd; returns false on error.
bool Write(int fd, const char *s) {
  size_t len = strlen(s);
  while (len > 0) {
    ssize_t w = write(fd, s, len);
    if (w < 0)
      return false;
    s += w;
    len -= static_cast<size_t>(w);
  }
  return true;
}

const char *SignalName(int sig) {
  switch (sig) {
  case SIGSEGV:
    return "SIGSEGV";
  case SIGABRT:
    return "SIGABRT";
  case SIGBUS:
    return "SIGBUS";
  case SIGILL:
    return "SIGILL";
  case SIGFPE:
    return "SIGFPE";
  default:
    return "UNKNOWN";
  }
}

void CrashSignalHandler(int sig, siginfo_t * /*info*/, void * /*context*/) {
  // Build path: <logDir>/crash_<epoch>.log
  // All operations here must be async-signal-safe.
  char path[4096 + 32];
  size_t dirLen = strlen(g_logDir);
  memcpy(path, g_logDir, dirLen);
  memcpy(path + dirLen, "/crash_", 7);
  dirLen += 7;

  // Append seconds since epoch using async-signal-safe clock_gettime.
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  char intBuf[32];
  int n = WriteInt(intBuf, sizeof(intBuf), static_cast<long long>(ts.tv_sec));
  memcpy(path + dirLen, intBuf, static_cast<size_t>(n));
  dirLen += static_cast<size_t>(n);
  memcpy(path + dirLen, ".log", 5); // includes NUL

  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    // Cannot open crash file; at least try stderr.
    fd = STDERR_FILENO;
  }

  Write(fd, "=== Kennel crash report ===\n");
  Write(fd, "Signal: ");
  Write(fd, SignalName(sig));
  Write(fd, " (");
  char sigBuf[8];
  int sn = WriteInt(sigBuf, sizeof(sigBuf), sig);
  sigBuf[sn] = '\0';
  Write(fd, sigBuf);
  Write(fd, ")\n\nBacktrace:\n");

  // backtrace / backtrace_symbols_fd are async-signal-safe on most platforms.
  void *frames[128];
  int count = backtrace(frames, 128);
  backtrace_symbols_fd(frames, count, fd);
  Write(fd, "\n=== end of crash report ===\n");

  if (fd != STDERR_FILENO) {
    close(fd);
  }

  // Re-raise with default handler so the process exits with a crash status
  // and core-dump generation is preserved.
  struct sigaction sa {};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(sig, &sa, nullptr);
  raise(sig);
}

} // namespace

void CrashHandler::Install(const wxString &logDir) {
  const char *dir = logDir.utf8_str();
  strncpy(g_logDir, dir, sizeof(g_logDir) - 1);
  g_logDir[sizeof(g_logDir) - 1] = '\0';

  struct sigaction sa {};
  sa.sa_sigaction = CrashSignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

  const int signals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
  for (int sig : signals) {
    sigaction(sig, &sa, nullptr);
  }
}

#else // non-POSIX stub

void CrashHandler::Install(const wxString & /*logDir*/) {}

#endif
