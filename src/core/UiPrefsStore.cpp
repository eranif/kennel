#include "core/UiPrefsStore.h"

#include "core/JsonUtil.h"
#include "core/Logger.h"

namespace {

using jsonutil::GetBool;
using jsonutil::GetInt;
using jsonutil::GetSizeT;
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
      {"auiLayout", p.auiLayout},
      {"sideTab", p.sideTab},
      {"scrollbackLines", p.scrollbackLines},
      {"tabHasCloseButton", p.tabHasCloseButton},
      {"sessionCloseAction", static_cast<int>(p.sessionCloseAction)},
      {"localAgentsScanned", p.localAgentsScanned},
      {"remoteScanOffered", p.remoteScanOffered},
      {"lastTerminalShell", ToUtf8(p.lastTerminalShell)},
      {"tabOutlineColour", p.tabOutlineColour},
      {"tabPosition", static_cast<int>(p.tabPosition)},
      {"tabHeight", static_cast<int>(p.tabHeight)},
      {"sideBarTabPosition", static_cast<int>(p.sideBarTabPosition)},
      {"lastSelectedSession", ToUtf8(p.lastSelectedSession)},
      {"terminalTheme", ToUtf8(p.terminalTheme)},
      {"terminalFontDesc", ToUtf8(p.terminalFontDesc)},
      {"appTheme", ToUtf8(p.appTheme)},
      {"terminalOptimizedDrawing", p.terminalOptimizedDrawing},
      {"checkForUpdatesOnStartup", p.checkForUpdatesOnStartup},
      {"closeReviewBuddyOnDismiss", p.closeReviewBuddyOnDismiss},
      {"recentWorkingDirs", toStrArr(p.recentWorkingDirs)},
      {"recentHosts", toStrArr(p.recentHosts)},
      {"blockCursor", p.blockCursor},
      {"wrapLines", p.wrapLines},
      {"groupTabs", p.groupTabs},
      {"gitSignoff", p.gitSignoff},
      {"highlightBraces", p.highlightBraces},
      {"highlightIndentLines", p.highlightIndentLines},
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
  p.auiLayout = GetStr(root, "auiLayout", p.auiLayout);
  p.sideTab = GetInt(root, "sideTab", p.sideTab);
  p.tabHasCloseButton = GetBool(root, "tabHasCloseButton", p.tabHasCloseButton);
  {
    const int action = GetInt(root, "sessionCloseAction",
                              static_cast<int>(p.sessionCloseAction));
    if (action >= static_cast<int>(CloseAction::Ask) &&
        action <= static_cast<int>(CloseAction::Close)) {
      p.sessionCloseAction = static_cast<CloseAction>(action);
    }
  }
  // A prefs file from before the agents were looked for is not a first run:
  // the user has set up Kennel already, and an agent they removed must not come
  // back. Only a missing file (no one ran Kennel here) is a first run.
  p.localAgentsScanned = GetBool(root, "localAgentsScanned", true);
  p.remoteScanOffered = GetBool(root, "remoteScanOffered", true);
  p.lastTerminalShell = GetStr(root, "lastTerminalShell", p.lastTerminalShell);
  p.tabOutlineColour = GetStr(root, "tabOutlineColour", p.tabOutlineColour);
  p.tabPosition = static_cast<wxDirection>(
      GetInt(root, "tabPosition", static_cast<int>(p.tabPosition)));
  {
    const int height = GetInt(root, "tabHeight", static_cast<int>(p.tabHeight));
    if (height >= static_cast<int>(TabHeight::Short) &&
        height <= static_cast<int>(TabHeight::Tall)) {
      p.tabHeight = static_cast<TabHeight>(height);
    }
  }
  p.sideBarTabPosition = static_cast<wxDirection>(GetInt(
      root, "sideBarTabPosition", static_cast<int>(p.sideBarTabPosition)));
  p.scrollbackLines = GetSizeT(root, "scrollbackLines", p.scrollbackLines);
  p.lastSelectedSession =
      GetStr(root, "lastSelectedSession", p.lastSelectedSession);
  p.terminalTheme = GetStr(root, "terminalTheme", p.terminalTheme);
  p.terminalFontDesc = GetStr(root, "terminalFontDesc", p.terminalFontDesc);
  p.appTheme = GetStr(root, "appTheme", p.appTheme);
  p.terminalLoginShell =
      GetStr(root, "terminalLoginShell", p.terminalLoginShell);
  p.terminalHomeDir = GetStr(root, "terminalHomeDir", p.terminalHomeDir);
  p.terminalOptimizedDrawing =
      GetBool(root, "terminalOptimizedDrawing", p.terminalOptimizedDrawing);
  p.blockCursor = GetBool(root, "blockCursor", p.blockCursor);
  p.wrapLines = GetBool(root, "wrapLines", p.wrapLines);
  p.groupTabs = GetBool(root, "groupTabs", p.groupTabs);
  p.gitSignoff = GetBool(root, "gitSignoff", p.gitSignoff);
  p.highlightBraces = GetBool(root, "highlightBraces", p.highlightBraces);
  p.highlightIndentLines =
      GetBool(root, "highlightIndentLines", p.highlightIndentLines);
  p.checkForUpdatesOnStartup =
      GetBool(root, "checkForUpdatesOnStartup", p.checkForUpdatesOnStartup);
  p.closeReviewBuddyOnDismiss =
      GetBool(root, "closeReviewBuddyOnDismiss", p.closeReviewBuddyOnDismiss);
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
