#pragma once

#include "app/AcceleratorInterceptor.h"
#include "app/TerminalManager.hpp"
#include "core/Account.h"

#include <map>
#include <memory>
#include <optional>
#include <wx/event.h>
#include <wx/string.h>

class wxSimplebook;
class wxTerminalEvent;

// The terminals of the git panels: the pages of the book of the Git tab of the
// Output View. OutputView owns the book and this object; the git panels know
// none of them. A panel asks for its terminal with the wxEVT_GITVIEW_* events
// (see GitPanel.hpp), which OutputView passes on to the methods below with the
// id of the panel (GitPanel::GetPanelId()).
//
// A panel has at most one terminal, made by its first command. Page 0 of the
// book is the empty page: the one that shows for a panel without a terminal.
class GitTerminals : public wxEvtHandler {
public:
  explicit GitTerminals(wxSimplebook *book);
  ~GitTerminals() override;

  // Runs `command` in the terminal of the panel `panelId`, and shows it. The
  // terminal is made first when there is none: a shell of `account` in `root`
  // (on a remote host, a shell of this machine that logs in first).
  void Run(int panelId, const Account &account, const wxString &root,
           const wxString &command);
  // Shows the terminal of the panel `panelId`, or the empty page.
  void Show(int panelId);
  // Deletes the terminal of the panel `panelId`, when it has one.
  void Remove(int panelId);

private:
  struct Terminal {
    wxTerminalViewCtrl *view{nullptr}; // A page of m_book
    std::unique_ptr<AcceleratorInterceptor> interceptor;
  };

  // How the terminal of `account` starts in `root`: a shell of this machine,
  // or the shell of the WSL distribution. Nothing when there is no shell for
  // it (the user is told).
  std::optional<TerminalOptions> GetTerminalOptions(const Account &account,
                                                    const wxString &root) const;
  // Makes the terminal of the panel `panelId` and starts it. Null when it
  // could not be made.
  wxTerminalViewCtrl *Create(int panelId, const Account &account,
                             const wxString &root);
  // The shell of a terminal ended: the terminal goes, the next command of its
  // panel makes a new one.
  void OnTerminated(wxTerminalEvent &event);
  void OnThemeApplied(wxCommandEvent &event);

  wxSimplebook *m_book{nullptr};
  // By the id of the panel.
  std::map<int, Terminal> m_terminals;
};
