#pragma once

#include <wx/string.h>

// Installs POSIX signal handlers that write a backtrace to the log directory
// on fatal crashes (SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE).
// No-op on non-POSIX platforms.
class CrashHandler {
public:
  // Registers signal handlers. Call once at startup, after the log
  // directory has been created.
  static void Install(const wxString &logDir);
};
