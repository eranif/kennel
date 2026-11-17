#pragma once

#include <wx/event.h>
#include <wx/string.h>
#include <wx/window.h>

class AcceleratorInterceptor {
public:
  AcceleratorInterceptor(wxWindow *ctrl);
  ~AcceleratorInterceptor();

protected:
  void OnCharHook(wxKeyEvent &keyEvent);

private:
  // The page that is shown can toggle its terminal pane (SessionPage::
  // CanToggleTerminalPane()).
  bool CanToggleSessionTerminal() const;
  // The id of the item of Edit > Bookmarks that the F2 key stands for while a
  // file is showing (F2, Shift+F2, Ctrl+F2, Ctrl+Shift+F2); empty for another
  // page or another modifier. F2 renames a session otherwise.
  wxString GetBookmarkMenuName(const wxKeyEvent &keyEvent) const;

  wxWindow *m_ctrl{nullptr};
};
