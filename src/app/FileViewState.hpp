#pragma once

// What the user sees and has selected in a file: the selection (anchor and
// caret, byte offsets in the document) and the first line on the screen. See
// FilePage::GetViewState().
struct FileViewState {
  int anchor{0};
  int caret{0};
  int firstLine{0};
};
