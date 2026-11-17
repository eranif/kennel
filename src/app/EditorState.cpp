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
  wxStyledTextCtrl *ctrl = m_editor.GetCtrl();
  m_editor.SetBookmarks(m_bookmarks);

  // The caret: the same line and column, or the nearest place that exists.
  const int line = std::clamp(m_caretLine, 0, ctrl->GetLineCount() - 1);
  const int start = ctrl->PositionFromLine(line);
  const int end = ctrl->GetLineEndPosition(line);
  ctrl->SetEmptySelection(
      std::min(ctrl->PositionRelative(start, m_caretColumn), end));
  ctrl->ChooseCaretX();

  // After the caret: moving it may scroll.
  ctrl->SetFirstVisibleLine(std::max(m_firstLine, 0));
}
