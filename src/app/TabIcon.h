#pragma once

#include <wx/bmpbndl.h>

// The icons of the tabs are made for menus (16 DIP). This bundle shows `source`
// at that size (16 DIP), and it makes the bitmap at the size that is asked for,
// so the icon is sharp when the display is scaled. An empty bundle gives an
// empty bundle. Used by every tab bar, so that their icons have the same size.
//
// A small mark in the lower right corner of the icon of a session that runs in
// tmux: the picture of tmux (assets/tmux.svg) while it runs in tmux, a yellow
// dot when the tab is Detached (see SessionStatus::Detached).
enum class TabBadge { kNone, kTmux, kDetached };

// With `modified`, an orange dot is drawn in the lower right corner (a file
// with changes that are not saved). A `badge` draws its own dot in the same
// place (`modified` comes first).
wxBitmapBundle TabIcon(const wxBitmapBundle &source, bool modified = false,
                       TabBadge badge = TabBadge::kNone);
