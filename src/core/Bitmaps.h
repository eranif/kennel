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

  static wxSize GetToolBarIconSize();

private:
  wxString FixName(const wxString &filename) const;

  BitmapsTable m_menuBitmaps;
  BitmapsTable m_toolbarBitmaps;
  wxBitmapBundle m_defaultToolBar;
  wxBitmapBundle m_defaultMenu;
  AliasTable m_aliases;
};
