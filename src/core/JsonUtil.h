#pragma once

#include <wx/string.h>

#include "core/json.hpp"

#include <string>
#include <vector>

// Shared JSON/file helpers for the persistence stores. Header-only so the
// heavy json.hpp include stays confined to translation units that opt in by
// including this file (never expose it through a public store header).

namespace jsonutil {

using nlohmann::json;

// --- wxString <-> std::string (UTF-8) -------------------------------------
inline std::string ToUtf8(const wxString &s) {
  return s.ToStdString(wxConvUTF8);
}
inline wxString FromUtf8(const std::string &s) { return wxString::FromUTF8(s); }

// --- file I/O --------------------------------------------------------------
// Reads a whole file into a UTF-8 std::string. Returns false on I/O failure.
bool ReadFileUtf8(const wxString &path, std::string *out);

// Writes UTF-8 text to a file, truncating. Returns false on I/O failure.
bool WriteFileUtf8(const wxString &path, const std::string &text);

// --- tolerant typed-field readers (missing/wrong type -> default) ----------
wxString GetStr(const json &j, const char *key, const wxString &dflt = "");
int GetInt(const json &j, const char *key, int dflt);
size_t GetSizeT(const json &j, const char *key, size_t dflt);
bool GetBool(const json &j, const char *key, bool dflt);
std::vector<wxString> GetStrArray(const json &j, const char *key);

} // namespace jsonutil
