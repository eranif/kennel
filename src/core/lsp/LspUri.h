#pragma once

#include <wx/string.h>

// The `file://` URI of a file, as the Language Server Protocol writes it.
// GUI-free.
class LspUri {
public:
  // `path` is a path on the machine of the server: a Linux path ("/home/me/x")
  // or a Windows one ("C:\x" or "C:/x").
  explicit LspUri(const wxString &path) : m_path(path) {}

  // "file:///home/me/x", "file:///C:/x". Everything but the letters, the
  // digits, `-._~` and `/` is written as %XX (UTF-8 bytes).
  wxString GetUri() const;

private:
  wxString m_path;
};
