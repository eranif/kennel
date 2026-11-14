#pragma once

#include <string>

// `text` as one word for a POSIX shell, in single quotes: nothing in it is
// special to the shell, not even `$` or a quote.
inline std::string ShellQuote(const std::string &text) {
  std::string quoted = "'";
  for (const char c : text) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  return quoted + "'";
}
