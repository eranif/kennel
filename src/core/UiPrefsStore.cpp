#include "core/UiPrefsStore.h"

#include "core/JsonUtil.h"
#include "core/Logger.h"

namespace {

using jsonutil::GetBool;
using jsonutil::GetInt;
using jsonutil::GetStr;
using jsonutil::ReadFileUtf8;
using jsonutil::ToUtf8;
using jsonutil::WriteFileUtf8;
using nlohmann::json;

json ToJson(const UiPrefs &p) {
  auto toStrArr = [](const std::vector<wxString> &v) {
    json arr = json::array();
    for (const auto &s : v) {
      arr.push_back(ToUtf8(s));
    }
    return arr;
  };
  return json{
      {"version", p.version},
      {"window",
       {{"x", p.window.x},
        {"y", p.window.y},
        {"width", p.window.width},
        {"height", p.window.height},
        {"maximized", p.window.maximized}}},
      {"sidebarWidth", p.sidebarWidth},
      {"lastSelectedSession", ToUtf8(p.lastSelectedSession)},
      {"terminalTheme", ToUtf8(p.terminalTheme)},
      {"terminalFontDesc", ToUtf8(p.terminalFontDesc)},
      {"terminalOptimizedDrawing", p.terminalOptimizedDrawing},
      {"checkForUpdatesOnStartup", p.checkForUpdatesOnStartup},
      {"recentWorkingDirs", toStrArr(p.recentWorkingDirs)},
      {"recentHosts", toStrArr(p.recentHosts)},
      {"blockCursor", p.blockCursor},
      {"terminalLoginShell", ToUtf8(p.terminalLoginShell)},
      {"terminalHomeDir", ToUtf8(p.terminalHomeDir)},
  };
}

void ParsePrefs(const json &root, UiPrefs &p) {
  p.version = GetInt(root, "version", p.version);
  if (auto it = root.find("window"); it != root.end() && it->is_object()) {
    p.window.x = GetInt(*it, "x", p.window.x);
    p.window.y = GetInt(*it, "y", p.window.y);
    p.window.width = GetInt(*it, "width", p.window.width);
    p.window.height = GetInt(*it, "height", p.window.height);
    p.window.maximized = GetBool(*it, "maximized", p.window.maximized);
  }
  p.sidebarWidth = GetInt(root, "sidebarWidth", p.sidebarWidth);
  p.lastSelectedSession =
      GetStr(root, "lastSelectedSession", p.lastSelectedSession);
  p.terminalTheme = GetStr(root, "terminalTheme", p.terminalTheme);
  p.terminalFontDesc = GetStr(root, "terminalFontDesc", p.terminalFontDesc);
  p.terminalLoginShell =
      GetStr(root, "terminalLoginShell", p.terminalLoginShell);
  p.terminalHomeDir = GetStr(root, "terminalHomeDir", p.terminalHomeDir);
  p.terminalOptimizedDrawing =
      GetBool(root, "terminalOptimizedDrawing", p.terminalOptimizedDrawing);
  p.blockCursor = GetBool(root, "blockCursor", p.blockCursor);
  p.checkForUpdatesOnStartup =
      GetBool(root, "checkForUpdatesOnStartup", p.checkForUpdatesOnStartup);
  auto parseStrArr = [&](const char *key, std::vector<wxString> &out) {
    if (auto it = root.find(key); it != root.end() && it->is_array()) {
      for (const auto &el : *it) {
        if (el.is_string()) {
          out.push_back(wxString::FromUTF8(el.get<std::string>()));
        }
      }
    }
  };
  parseStrArr("recentWorkingDirs", p.recentWorkingDirs);
  parseStrArr("recentHosts", p.recentHosts);
}

} // namespace

UiPrefsStore::UiPrefsStore(AppPaths paths) : m_paths(std::move(paths)) {}

Status UiPrefsStore::Load(UiPrefs &prefs) {
  const wxString path = m_paths.PersistFile().GetFullPath();

  if (!wxFileExists(path)) {
    return Status::Error("Prefs file not found");
  }

  std::string raw;
  if (!ReadFileUtf8(path, &raw)) {
    KLOG_WARN() << "Could not read .persist.json; using default UI prefs";
    return Status::Error("Failed to read prefs file");
  }

  json root = json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    return Status::Error(
        "Ignoring corrupt .persist.json; using default UI prefs");
  }

  ParsePrefs(root, prefs);
  return Status::Ok();
}

Status UiPrefsStore::Save(const UiPrefs &prefs) {
  const wxString path = m_paths.PersistFile().GetFullPath();
  const std::string text = ToJson(prefs).dump(2);
  if (!WriteFileUtf8(path, text)) {
    return Status::Error(
        wxString::Format("Could not write UI prefs file: %s", path));
  }
  return Status::Ok();
}
