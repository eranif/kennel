#pragma once

#include <unordered_map>
#include <wx/bmpbndl.h>
#include <wx/string.h>

using BitmapsTable = std::unordered_map<wxString, wxBitmapBundle>;
using AliasTable = std::unordered_map<wxString, wxString>;

class Bitmaps {
public:
  Bitmaps();
  ~Bitmaps() = default;

  const wxBitmapBundle &Get(const wxString &name, bool forToolBar) const;
  const wxBitmapBundle &GetByAlias(const wxString &name, bool forToolBar) const;

  void Load(const wxString &filename);
  void AddAlias(const wxString &filename, const wxString &alias);

  // Loads the file type icons: assets/mime/dark/mime-<name>.svg and
  // assets/mime/light/mime-<name>.svg (the dark set is for a dark UI). Call it
  // once at startup.
  void LoadMimeIcons();
  // The icon of the file at `path` (a local or a remote path), by its extension
  // or its name, from the dark or the light set. A file type without an icon
  // gets the one for plain text, and then the generic file icon.
  const wxBitmapBundle &GetFileIcon(const wxString &path, bool dark) const;

  static wxSize GetToolBarIconSize();

private:
  wxString FixName(const wxString &filename) const;

  BitmapsTable m_menuBitmaps;
  BitmapsTable m_toolbarBitmaps;
  BitmapsTable m_mimeDark; // By name: "cpp", "txt", ...
  BitmapsTable m_mimeLight;
  wxBitmapBundle m_defaultToolBar;
  wxBitmapBundle m_defaultMenu;
  AliasTable m_aliases;
};
