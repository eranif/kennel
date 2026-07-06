#include "core/JsonUtil.h"

#include <wx/ffile.h>

namespace jsonutil {

bool ReadFileUtf8(const wxString &path, std::string *out) {
  wxFFile f(path, "rb");
  if (!f.IsOpened()) {
    return false;
  }
  wxString contents;
  if (!f.ReadAll(&contents, wxConvUTF8)) {
    return false;
  }
  *out = ToUtf8(contents);
  return true;
}

bool WriteFileUtf8(const wxString &path, const std::string &text) {
  wxFFile f(path, "wb");
  if (!f.IsOpened()) {
    return false;
  }
  return f.Write(FromUtf8(text), wxConvUTF8) && f.Close();
}

wxString GetStr(const json &j, const char *key, const wxString &dflt) {
  if (auto it = j.find(key); it != j.end() && it->is_string()) {
    return FromUtf8(it->get<std::string>());
  }
  return dflt;
}

int GetInt(const json &j, const char *key, int dflt) {
  if (auto it = j.find(key); it != j.end() && it->is_number_integer()) {
    return it->get<int>();
  }
  return dflt;
}

bool GetBool(const json &j, const char *key, bool dflt) {
  if (auto it = j.find(key); it != j.end() && it->is_boolean()) {
    return it->get<bool>();
  }
  return dflt;
}

std::vector<wxString> GetStrArray(const json &j, const char *key) {
  std::vector<wxString> out;
  if (auto it = j.find(key); it != j.end() && it->is_array()) {
    for (const auto &e : *it) {
      if (e.is_string()) {
        out.push_back(FromUtf8(e.get<std::string>()));
      }
    }
  }
  return out;
}

} // namespace jsonutil
