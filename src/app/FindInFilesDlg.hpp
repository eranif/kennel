#pragma once

#include "UI.hpp"
#include "core/Account.h"
#include "core/FindInFiles.h"
#include "core/RemoteInstaller.h"

// What to search for, and where (File -> Find in Files, Ctrl+Shift+F).
//
// The place is the machine and folder that the owner gives (SetTarget()). The
// moment it does, the dialog finds out whether ripgrep is on that machine: the
// RemoteInstaller checks for the right version, whatever the machine is (this
// one included). When ripgrep is missing the user is asked before anything is
// installed. The status line says what is going on, and Find stays disabled
// until ripgrep is there.
//
// The dialog does not search: when the user presses Find, the owner reads
// GetAccount() and GetQuery() and starts the search (FindResultsView).
class FindInFilesDlg : public FindInFilesDlgBase {
public:
  explicit FindInFilesDlg(wxWindow *parent);
  ~FindInFilesDlg() override;

  // Where to search. Starts the check of ripgrep on `account`.
  void SetTarget(const Account &account, const wxString &folder);
  // Fills in what the user typed last time: the pattern, the options and the
  // globs. The folder is not part of it (SetTarget() has it).
  void SetQuery(const FindQuery &query);

  const Account &GetAccount() const { return m_account; }
  FindQuery GetQuery() const;

protected:
  void OnOkUI(wxUpdateUIEvent &event) override;

private:
  // Finds out whether ripgrep is on the machine of m_account, and installs it
  // when Kennel is the one that does.
  void CheckRipgrep();
  // ripgrep is not there: asks the user whether to install it, and installs it
  // when the answer is yes.
  void AskToInstall();
  void OnInstallProgress(InstallEvent &event);
  void OnInstallDone(InstallEvent &event);
  void SetStatus(const wxString &text);

  RemoteInstaller m_installer;
  Account m_account;
  // ripgrep is on the machine: the answer of the check, which nothing else
  // holds.
  bool m_ready{false};
};
