#include "Bitmaps.h"
#include "app/AssetBootstrap.h"
#include "core/Logger.h"
#include <wx/artprov.h>
#include <wx/dir.h>
#include <wx/filename.h>

#ifdef __WXMSW__
constexpr int kToolIconSize = 24;
#elif defined(__WXGTK__)
constexpr int kToolIconSize = 32;
#else
constexpr int kToolIconSize = 24;
#endif

static const wxSize iconSize(kToolIconSize, kToolIconSize);
static const wxSize menuSize(16, 16);

Bitmaps::Bitmaps() {
  m_defaultToolBar = wxArtProvider::GetBitmapBundle(wxART_EXECUTABLE_FILE,
                                                    wxART_TOOLBAR, iconSize);
  m_defaultMenu = wxArtProvider::GetBitmapBundle(wxART_EXECUTABLE_FILE,
                                                 wxART_MENU, menuSize);
}

const wxBitmapBundle &Bitmaps::Get(const wxString &name,
                                   bool forToolBar) const {
  wxString fixedName = FixName(name);
  const BitmapsTable &table = forToolBar ? m_toolbarBitmaps : m_menuBitmaps;
  if (!table.contains(fixedName)) {
    return forToolBar ? m_defaultToolBar : m_defaultMenu;
  }
  return table.find(fixedName)->second;
}

void Bitmaps::Load(const wxString &filename) {
  wxString fixedName = FixName(filename);
  const wxString icon_path = ResolveIconPath(filename);
  wxFileName fn{icon_path};

  if (icon_path.empty()) {
    KLOG_WARN() << "Failed to load SVG file: " << icon_path << ". Empty path";
    return;
  }

  if (!fn.FileExists()) {
    KLOG_WARN() << "Failed to load SVG file: " << fn.GetFullPath()
                << ". No such file";
    return;
  }

  m_menuBitmaps.erase(fixedName);
  m_toolbarBitmaps.erase(fixedName);
  m_menuBitmaps.insert(std::make_pair(
      fixedName, wxBitmapBundle::FromSVGFile(fn.GetFullPath(), menuSize)));
  m_toolbarBitmaps.insert(std::make_pair(
      fixedName, wxBitmapBundle::FromSVGFile(fn.GetFullPath(), iconSize)));
}

wxSize Bitmaps::GetToolBarIconSize() {
  return wxSize(kToolIconSize, kToolIconSize);
}

void Bitmaps::AddAlias(const wxString &filename, const wxString &alias) {
  m_aliases.erase(alias);
  m_aliases.insert({alias, FixName(filename)});
}

wxString Bitmaps::FixName(const wxString &filename) const {
  wxFileName nameWithExt{filename};
  if (nameWithExt.GetExt().empty()) {
    nameWithExt.SetExt("svg");
  }
  return nameWithExt.GetFullName();
}

const wxBitmapBundle &Bitmaps::GetByAlias(const wxString &name,
                                          bool forToolBar) const {
  if (!m_aliases.contains(name)) {
    return Get("__NO_SUCH_BITMAP__", forToolBar);
  }
  return Get(m_aliases.find(name)->second, forToolBar);
}

namespace {
// The name of the mime icon (mime-<name>.svg) of a file, by its file name.
wxString MimeNameFor(const wxString &path) {
  const wxString file = path.AfterLast('/').AfterLast('\\').Lower();

  // Dotfiles have no extension: ".bashrc" is a shell script.
  static const std::unordered_map<wxString, wxString> kByName = {
      {".bashrc", "sh"},
      {".bash_profile", "sh"},
      {".bash_aliases", "sh"},
      {".bash_logout", "sh"},
      {".zshrc", "sh"},
      {".zprofile", "sh"},
      {".zshenv", "sh"},
      {".profile", "sh"},
      // Its extension is "txt", but it is a CMake file.
      {"cmakelists.txt", "cmake"},
  };
  if (auto it = kByName.find(file); it != kByName.end()) {
    return it->second;
  }

  static const std::unordered_map<wxString, wxString> kByExt = {
      {"as", "as"},     {"bmp", "bmp"},    {"c", "c"},
      {"cpp", "cpp"},   {"cc", "cpp"},     {"cxx", "cpp"},
      {"c++", "cpp"},   {"cp", "cpp"},     {"h", "h"},
      {"hpp", "h"},     {"hh", "h"},       {"hxx", "h"},
      {"h++", "h"},     {"cs", "csharp"},  {"css", "css"},
      {"scss", "css"},  {"less", "css"},   {"go", "go"},
      {"html", "html"}, {"htm", "html"},   {"java", "java"},
      {"js", "js"},     {"mjs", "js"},     {"cjs", "js"},
      {"jsx", "js"},    {"md", "md"},      {"markdown", "md"},
      {"php", "php"},   {"py", "python"},  {"pyw", "python"},
      {"sh", "sh"},     {"bash", "sh"},    {"zsh", "sh"},
      {"ksh", "sh"},    {"fish", "sh"},    {"sql", "sql"},
      {"svg", "svg"},   {"ts", "ts"},      {"tsx", "ts"},
      {"txt", "txt"},   {"log", "txt"},    {"text", "txt"},
      {"xml", "xml"},   {"xsl", "xml"},    {"xslt", "xml"},
      {"xsd", "xml"},   {"plist", "xml"},  {"cmake", "cmake"},
      {"json", "json"}, {"jsonc", "json"},
  };
  const int dot = file.Find('.', /*fromEnd=*/true);
  if (dot > 0) {
    if (auto it = kByExt.find(file.Mid(dot + 1)); it != kByExt.end()) {
      return it->second;
    }
  }
  return "txt";
}
} // namespace

void Bitmaps::LoadMimeIcons() {
  const wxString assets = ShippedAssetsDir().GetPath();
  if (assets.empty()) {
    KLOG_WARN() << "Cannot load the file type icons: no assets folder";
    return;
  }

  static const wxString kPrefix = "mime-";
  for (const bool dark : {true, false}) {
    BitmapsTable &table = dark ? m_mimeDark : m_mimeLight;
    table.clear();
    const wxString dir = assets + "/mime/" + (dark ? "dark" : "light");
    wxArrayString files;
    wxDir::GetAllFiles(dir, &files, "mime-*.svg", wxDIR_FILES);
    for (const wxString &file : files) {
      const wxFileName fn{file};
      table[fn.GetName().Mid(kPrefix.length())] =
          wxBitmapBundle::FromSVGFile(fn.GetFullPath(), menuSize);
    }
    KLOG_DEBUG() << "Loaded " << table.size() << " file type icons from "
                 << dir;
  }
}

const wxBitmapBundle &Bitmaps::GetFileIcon(const wxString &path,
                                           bool dark) const {
  const BitmapsTable &table = dark ? m_mimeDark : m_mimeLight;
  if (auto it = table.find(MimeNameFor(path)); it != table.end()) {
    return it->second;
  }
  // No icon for this type: plain text, and then the generic file icon.
  if (auto it = table.find("txt"); it != table.end()) {
    return it->second;
  }
  return GetByAlias("file", false);
}
