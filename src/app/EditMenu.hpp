#pragma once

#include <optional>
#include <wx/event.h>

class MainNotebook;
class wxFrame;
class wxMenu;
class wxMenuBar;

// The Edit menu:
//
//   Close Editor
//   Reload File
//   Undo, Redo
//   Copy, Paste, Select All, Delete Line, Duplicate Line
//   [x] Wrap Lines, [x] Highlight Braces, [x] Highlight Indent Lines
//
// The menu is in the menu bar only while a file is showing, so its keys
// (Ctrl+W, Ctrl+R, Ctrl+Z, Ctrl+C, Ctrl+D...) are free for a terminal the rest
// of the time. The actions act on the editor of that file and need its focus
// (the text boxes of the page keep their keys); Close Editor and Reload File
// do not. The three
// options are in the preferences; a change is saved and sent to the open
// editors.
class EditMenu : public wxEvtHandler {
public:
  // `frame` gets the events of the items; `book` has the pages. Both must stay
  // alive as long as this object.
  EditMenu(wxFrame *frame, MainNotebook *book);
  ~EditMenu() override;

  // Makes the menu and remembers where it goes: at the end of `menuBar` now.
  // It is added to the bar and taken out again as the page changes.
  void AddTo(wxMenuBar *menuBar);

private:
  enum class Action {
    kClose,
    kReload,
    kUndo,
    kRedo,
    kCopy,
    kPaste,
    kSelectAll,
    kDeleteLine,
    kDuplicateLine
  };
  static std::optional<Action> ActionOf(int id);
  bool CanDo(Action action) const;
  void Do(Action action);

  void OnAction(wxCommandEvent &event);
  void OnActionUI(wxUpdateUIEvent &event);
  void OnOption(wxCommandEvent &event);
  void OnOptionUI(wxUpdateUIEvent &event);
  // Adds the menu to the bar while a file is showing, and takes it out
  // otherwise.
  void OnIdle(wxIdleEvent &event);

  wxFrame *m_frame;
  MainNotebook *m_book;
  wxMenuBar *m_menuBar{nullptr};
  wxMenu *m_menu{nullptr}; // The bar owns it while it is in the bar
  size_t m_position{0};    // Where the menu is in the bar
  bool m_inBar{false};
};
