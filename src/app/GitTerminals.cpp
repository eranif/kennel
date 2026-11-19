#include "app/GitTerminals.hpp"

#include "ThemeManager.h"
#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/EventNotifier.hpp"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "core/WslPath.h"
#include "terminal_event.h"
#include "terminal_view.h"

#include <wx/msgdlg.h>
#include <wx/simplebook.h>

GitTerminals::GitTerminals(wxSimplebook *book) : m_book(book) {
  EventNotifier::Get()->Bind(wxEVT_THEME_APPLIED, &GitTerminals::OnThemeApplied,
                             this);
}

GitTerminals::~GitTerminals() {
  EventNotifier::Get()->Unbind(wxEVT_THEME_APPLIED,
                               &GitTerminals::OnThemeApplied, this);
  // The terminals go now, while this object that handles their events is still
  // there (the book itself goes later, with the window).
  while (!m_terminals.empty()) {
    Remove(m_terminals.begin()->first);
  }
}

void GitTerminals::Run(int panelId, const Account &account,
                       const wxString &root, const wxString &command) {
  wxTerminalViewCtrl *view = nullptr;
  if (const auto it = m_terminals.find(panelId); it != m_terminals.end()) {
    view = it->second.view;
  } else {
    view = Create(panelId, account, root);
  }
  if (view == nullptr) {
    return;
  }
  // The panel that runs a command is the one that shows.
  Show(panelId);
  // It waits in the terminal until the shell runs.
  view->SendCommand(command);
}

void GitTerminals::Show(int panelId) {
  int page = 0; // The empty page
  if (const auto it = m_terminals.find(panelId); it != m_terminals.end()) {
    if (const int found = m_book->FindPage(it->second.view);
        found != wxNOT_FOUND) {
      page = found;
    }
  }
  if (page != m_book->GetSelection()) {
    m_book->ChangeSelection(page);
  }
}

void GitTerminals::Remove(int panelId) {
  const auto it = m_terminals.find(panelId);
  if (it == m_terminals.end()) {
    return;
  }
  wxTerminalViewCtrl *view = it->second.view;
  view->Unbind(wxEVT_TERMINAL_TERMINATED, &GitTerminals::OnTerminated, this);
  if (const int page = m_book->FindPage(view); page != wxNOT_FOUND) {
    const bool shown = m_book->GetSelection() == page;
    m_book->DeletePage(page);
    // The book would show the page next to it, the terminal of another panel:
    // it shows the empty page.
    if (shown) {
      m_book->ChangeSelection(0);
    }
  } else {
    view->Destroy();
  }
  // After the terminal: it holds a handler of the interceptor.
  m_terminals.erase(it);
}

std::optional<TerminalOptions>
GitTerminals::GetTerminalOptions(const Account &account,
                                 const wxString &root) const {
  TerminalOptions options;
  options.shellCommand = AppManager::Get().GetPrefs().terminalLoginShell;
  options.workingDir = root;
  if (account.IsWSL()) {
    // The shell of the distribution, as the list of terminals has it.
    options.shellCommand.clear();
    for (const auto &[name, command] : ::FindShells().shells) {
      if (wsl::DistroOf(command) == account.m_name) {
        options.shellCommand = command;
        break;
      }
    }
    if (options.shellCommand.empty()) {
      KLOG_WARN() << "No shell for the WSL distribution " << account.m_name;
      wxMessageBox(wxString::Format(_("No shell found for '%s'"),
                                    account.GetDisplayName()),
                   "Kennel", wxOK | wxICON_ERROR);
      return std::nullopt;
    }
  } else if (account.IsLocalHost()) {
    options.cwd = root;
  }
  // A remote host: the shell of this machine, which logs in first (Create()).
  return options;
}

wxTerminalViewCtrl *GitTerminals::Create(int panelId, const Account &account,
                                         const wxString &root) {
  const std::optional<TerminalOptions> options =
      GetTerminalOptions(account, root);
  if (!options) {
    return nullptr;
  }
  wxTerminalViewCtrl *view =
      TerminalManager::Get().CreateTerminal(m_book, *options);
  m_book->AddPage(view, account.GetDisplayName(), /*select=*/false);
  view->Bind(wxEVT_TERMINAL_TERMINATED, &GitTerminals::OnTerminated, this);
  Terminal &terminal = m_terminals[panelId];
  terminal.view = view;
  terminal.interceptor = std::make_unique<AcceleratorInterceptor>(view);
  view->EnsureStarted();
  if (account.IsRemote()) {
    view->SendCommand(SshLoginCommand(account.m_name, account.m_user));
  }
  return view;
}

void GitTerminals::OnTerminated(wxTerminalEvent &event) {
  event.Skip();
  for (const auto &[panelId, terminal] : m_terminals) {
    if (terminal.view == event.GetEventObject()) {
      // Deferred: the terminal is destroyed by it.
      CallAfter([this, id = panelId] { Remove(id); });
      return;
    }
  }
}

void GitTerminals::OnThemeApplied(wxCommandEvent &event) {
  event.Skip();
  const auto theme = ThemeManager::Get().ActiveTheme();
  if (!theme) {
    return;
  }
  for (const auto &[panelId, terminal] : m_terminals) {
    terminal.view->SetTheme(*theme);
  }
}
