#pragma once

#include "core/UiPrefs.h"

// The vertical padding of a tab of a book (GlyphBook::SetTabPaddingY()) for a
// tab height, in DIPs. Explicit for every height, so that the heights look the
// same on every platform (the default of the book is 6 on Windows and macOS and
// 10 on GTK).
inline int TabPaddingOf(TabHeight height) {
  switch (height) {
  case TabHeight::Short:
    return 4;
  case TabHeight::Small:
    return 6;
  case TabHeight::Medium:
    return 8;
  case TabHeight::Tall:
    return 10;
  }
  return 8;
}
