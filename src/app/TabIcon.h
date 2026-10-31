#pragma once

#include <wx/bmpbndl.h>

// The icons of the tabs are made for menus (16 DIP). This bundle shows `source`
// bigger (24 DIP), and it makes the bitmap at the size that is asked for, so
// the icon is sharp when the display is scaled. An empty bundle gives an empty
// bundle. Used by every tab bar, so that their icons have the same size.
wxBitmapBundle TabIcon(const wxBitmapBundle &source);
