#include "Bitmaps.h"
#include "app/AssetBootstrap.h"
#include "core/Logger.h"
#include <wx/artprov.h>
#include <wx/filename.h>

#ifdef __WXMSW__
constexpr int kToolIconSize = 24;
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
