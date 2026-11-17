#include "app/EditorState.hpp"

#include "app/Editor.h"

#include <algorithm>

EditorState::EditorState(Editor &editor) : m_editor{editor} {
  wxStyledTextCtrl *ctrl = m_editor.GetCtrl();
  m_bookmarks = m_editor.GetBookmarks();
  const int caret = ctrl->GetCurrentPos();
  m_caretLine = ctrl->LineFromPosition(caret);
  m_caretColumn =
      ctrl->CountCharacters(ctrl->PositionFromLine(m_caretLine), caret);
  m_firstLine = ctrl->GetFirstVisibleLine();
}

EditorState::~EditorState() {
  if (m_discarded) {
    return;
  }
  wxStyledTextCtrl *ctrl = m_editor.GetCtrl();
  m_editor.SetBookmarks(m_bookmarks);

  // The caret: the same line and column, or the nearest place that exists.
  const int line = std::clamp(m_caretLine, 0, ctrl->GetLineCount() - 1);
  const int start = ctrl->PositionFromLine(line);
  const int end = ctrl->GetLineEndPosition(line);
  // The column is cut to the line first: PositionRelative() gives 0 (the start
  // of the file) when it goes past the end of the text.
  const int column = std::min(m_caretColumn, ctrl->CountCharacters(start, end));
  ctrl->SetEmptySelection(ctrl->PositionRelative(start, column));
  ctrl->ChooseCaretX();

  // After the caret: moving it may scroll.
  ctrl->SetFirstVisibleLine(std::max(m_firstLine, 0));
}
