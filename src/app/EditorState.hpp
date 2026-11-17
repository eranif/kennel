#pragma once

#include <vector>

class Editor;

// What the user has set up in an editor, kept while the text is loaded again
// (a reload of the file) and put back afterwards. It captures the state when it
// is made and restores it when it goes out of scope:
//
//   {
//     EditorState state{editor};
//     editor.LoadFile(path);
//   } // the bookmarks, the caret and the first line are as they were
//
// For a text that arrives later (a remote file) the object is kept until it
// does, and destroyed after the text was set.
//
// It holds the bookmarks (as line numbers), the caret (its line and its column,
// counted in characters) and the first line that is shown. A line that is gone
// from the new text is dropped (a bookmark) or the nearest one is taken. The
// editor must outlive the object.
class EditorState {
public:
  // Captures the state of `editor`.
  explicit EditorState(Editor &editor);
  // Puts the captured state back.
  ~EditorState();

  // The state is not put back when the object goes away (a reload that gave no
  // text).
  void Discard() { m_discarded = true; }

  EditorState(const EditorState &) = delete;
  EditorState &operator=(const EditorState &) = delete;

private:
  Editor &m_editor;
  std::vector<int> m_bookmarks;
  int m_caretLine{0};
  int m_caretColumn{0}; // In characters from the start of the line
  int m_firstLine{0};
  bool m_discarded{false};
};
