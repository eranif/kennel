#include "core/lsp/LspUri.h"

#include <cctype>
#include <string>

wxString LspUri::GetUri() const {
  std::string path = m_path.ToStdString(wxConvUTF8);
  for (char &c : path) {
    if (c == '\\') {
      c = '/';
    }
  }
  const bool drive = path.size() >= 2 &&
                     std::isalpha(static_cast<unsigned char>(path[0])) &&
                     path[1] == ':';

  std::string uri = "file://";
  if (drive || path.empty() || path[0] != '/') {
    uri += '/';
  }
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < path.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(path[i]);
    if ((drive && i == 1) || c == '/' || c == '-' || c == '.' || c == '_' ||
        c == '~' || std::isalnum(c)) {
      uri += static_cast<char>(c);
      continue;
    }
    uri += '%';
    uri += kHex[c >> 4];
    uri += kHex[c & 0x0F];
  }
  return wxString::FromUTF8(uri);
}
