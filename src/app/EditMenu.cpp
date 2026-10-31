#include "app/EditMenu.hpp"

#include "app/Editor.h"
#include "app/FilePage.hpp"
#include "app/MainNotebook.hpp"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Logger.h"

#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/xrc/xmlres.h>

EditMenu::EditMenu(wxFrame *frame, MainNotebook *book)
    : m_frame{frame}, m_book{book} {}

EditMenu::~EditMenu() {
  // Not in the bar: nobody else owns it.
  if (!m_inBar) {
    delete m_menu;
  }
}

void EditMenu::AddTo(wxMenuBar *menuBar) {
  m_menuBar = menuBar;
  m_position = menuBar->GetMenuCount();
  m_menu = new wxMenu();
  wxMenu *menu = m_menu;
  menu->Append(XRCID("edit-reload-file"), _("Reload File\tCtrl+R"),
               _("Load the file that is showing again"));
  menu->AppendSeparator();
  menu->Append(XRCID("edit-undo"), _("Undo\tCtrl+Z"));
  menu->Append(XRCID("edit-redo"), _("Redo\tCtrl+Y"));
  menu->AppendSeparator();
  menu->Append(XRCID("edit-copy"), _("Copy\tCtrl+C"));
  menu->Append(XRCID("edit-paste"), _("Paste\tCtrl+V"));
  menu->Append(XRCID("edit-select-all"), _("Select All\tCtrl+A"));
  menu->Append(XRCID("edit-delete-line"), _("Delete Line\tCtrl+L"));
  menu->Append(XRCID("edit-duplicate-line"), _("Duplicate Line\tCtrl+D"));
  for (const char *name :
       {"edit-reload-file", "edit-undo", "edit-redo", "edit-copy", "edit-paste",
        "edit-select-all", "edit-delete-line", "edit-duplicate-line"}) {
    m_frame->Bind(wxEVT_MENU, &EditMenu::OnAction, this, XRCID(name));
    m_frame->Bind(wxEVT_UPDATE_UI, &EditMenu::OnActionUI, this, XRCID(name));
  }

  menu->AppendSeparator();
  menu->AppendCheckItem(XRCID("edit-wrap-lines"), _("Wrap Lines"));
  menu->AppendCheckItem(XRCID("edit-highlight-braces"), _("Highlight Braces"));
  menu->AppendCheckItem(XRCID("edit-highlight-indent"),
                        _("Highlight Indent Lines"));
  for (const char *name :
       {"edit-wrap-lines", "edit-highlight-braces", "edit-highlight-indent"}) {
    m_frame->Bind(wxEVT_MENU, &EditMenu::OnOption, this, XRCID(name));
    m_frame->Bind(wxEVT_UPDATE_UI, &EditMenu::OnOptionUI, this, XRCID(name));
  }

  // No file is showing yet: the menu stays out of the bar until one is.
  m_frame->Bind(wxEVT_IDLE, &EditMenu::OnIdle, this);
}

void EditMenu::OnIdle(wxIdleEvent &event) {
  event.Skip();
  const bool wanted = m_book->GetActiveFilePage() != nullptr;
  if (wanted == m_inBar) {
    return;
  }
  if (wanted) {
    m_menuBar->Insert(m_position, m_menu, _("&Edit"));
  } else {
    m_menuBar->Remove(m_position);
  }
  m_inBar = wanted;
}

std::optional<EditMenu::Action> EditMenu::ActionOf(int id) {
  if (id == XRCID("edit-reload-file")) {
    return Action::kReload;
  }
  if (id == XRCID("edit-undo")) {
    return Action::kUndo;
  }
  if (id == XRCID("edit-redo")) {
    return Action::kRedo;
  }
  if (id == XRCID("edit-copy")) {
    return Action::kCopy;
  }
  if (id == XRCID("edit-paste")) {
    return Action::kPaste;
  }
  if (id == XRCID("edit-select-all")) {
    return Action::kSelectAll;
  }
  if (id == XRCID("edit-delete-line")) {
    return Action::kDeleteLine;
  }
  if (id == XRCID("edit-duplicate-line")) {
    return Action::kDuplicateLine;
  }
  return std::nullopt;
}

bool EditMenu::CanDo(Action action) const {
  const FilePage *file = m_book->GetActiveFilePage();
  if (file == nullptr) {
    return false;
  }
  if (action == Action::kReload) {
    return true;
  }
  // The rest only while the editor has the focus: the text boxes of the page
  // (the search bar) keep Ctrl+C, Ctrl+V...
  if (!file->EditorHasFocus()) {
    return false;
  }
  switch (action) {
  case Action::kUndo:
    return file->CanUndo();
  case Action::kRedo:
    return file->CanRedo();
  default:
    return true;
  }
}

void EditMenu::Do(Action action) {
  FilePage *file = m_book->GetActiveFilePage();
  if (file == nullptr) {
    return;
  }
  switch (action) {
  case Action::kReload:
    file->Reload();
    break;
  case Action::kUndo:
    file->Undo();
    break;
  case Action::kRedo:
    file->Redo();
    break;
  case Action::kCopy:
    file->Copy();
    break;
  case Action::kPaste:
    file->Paste();
    break;
  case Action::kSelectAll:
    file->SelectAll();
    break;
  case Action::kDeleteLine:
    file->DeleteLine();
    break;
  case Action::kDuplicateLine:
    file->DuplicateLine();
    break;
  }
}

void EditMenu::OnAction(wxCommandEvent &event) {
  if (auto action = ActionOf(event.GetId())) {
    Do(*action);
  }
}

void EditMenu::OnActionUI(wxUpdateUIEvent &event) {
  const auto action = ActionOf(event.GetId());
  event.Enable(action.has_value() && CanDo(*action));
}

void EditMenu::OnOption(wxCommandEvent &event) {
  auto &prefs = AppManager::Get().GetPrefs();
  const int id = event.GetId();
  if (id == XRCID("edit-wrap-lines")) {
    prefs.wrapLines = !prefs.wrapLines;
    // The file pages follow, and so does the Wrap Lines tool of their toolbar.
    EventNotifier::Get()->AddPendingEvent(
        wxCommandEvent(wxEVT_WRAP_LINES_CHANGED));
  } else {
    if (id == XRCID("edit-highlight-braces")) {
      prefs.highlightBraces = !prefs.highlightBraces;
    } else {
      prefs.highlightIndentLines = !prefs.highlightIndentLines;
    }
    EventNotifier::Get()->AddPendingEvent(
        wxCommandEvent(wxEVT_EDITOR_OPTIONS_CHANGED));
  }
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not save the editor option: " << st.message();
  }
}

void EditMenu::OnOptionUI(wxUpdateUIEvent &event) {
  const auto &prefs = AppManager::Get().GetPrefs();
  const int id = event.GetId();
  if (id == XRCID("edit-wrap-lines")) {
    event.Check(prefs.wrapLines);
  } else if (id == XRCID("edit-highlight-braces")) {
    event.Check(prefs.highlightBraces);
  } else {
    event.Check(prefs.highlightIndentLines);
  }
}
