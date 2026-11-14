#include "FindInFilesDlg.hpp"

#include "core/Ripgrep.h"

#include <wx/msgdlg.h>

namespace {
// The width the status line wraps at, in DIP.
constexpr int kStatusWrapDip = 460;
} // namespace

FindInFilesDlg::FindInFilesDlg(wxWindow *parent)
    : FindInFilesDlgBase(parent), m_installer(this) {
  Bind(wxEVT_INSTALL_PROGRESS, &FindInFilesDlg::OnInstallProgress, this);
  Bind(wxEVT_INSTALL_DONE, &FindInFilesDlg::OnInstallDone, this);
  m_textCtrlPattern->SetFocus();
}

FindInFilesDlg::~FindInFilesDlg() = default;

void FindInFilesDlg::SetTarget(const Account &account, const wxString &folder) {
  m_account = account;
  m_textCtrlFolder->SetValue(folder);
  if (folder.empty()) {
    // A remote host or a WSL distribution that has no folder to start in: Find
    // stays disabled until one is typed, so ask for it there.
    m_textCtrlFolder->SetHint(wxString::Format(
        _("Type the folder to search on %s"), account.GetDisplayName()));
    m_textCtrlFolder->SetFocus();
  }
  m_staticTextHost->SetLabel(account.GetDisplayName());
  CheckRipgrep();
}

void FindInFilesDlg::SetQuery(const FindQuery &query) {
  m_textCtrlPattern->SetValue(query.pattern);
  m_textCtrlPattern->SelectAll();
  m_checkBoxRegex->SetValue(query.isRegex);
  m_checkBoxMatchCase->SetValue(query.matchCase);
  m_checkBoxWholeWord->SetValue(query.wholeWord);
  m_textCtrlInclude->SetValue(query.include);
  m_textCtrlExclude->SetValue(query.exclude);
}

FindQuery FindInFilesDlg::GetQuery() const {
  FindQuery query;
  query.pattern = m_textCtrlPattern->GetValue();
  query.isRegex = m_checkBoxRegex->GetValue();
  query.matchCase = m_checkBoxMatchCase->GetValue();
  query.wholeWord = m_checkBoxWholeWord->GetValue();
  query.include = m_textCtrlInclude->GetValue().Trim().Trim(false);
  query.exclude = m_textCtrlExclude->GetValue().Trim().Trim(false);
  query.root = m_textCtrlFolder->GetValue().Trim().Trim(false);
  return query;
}

void FindInFilesDlg::OnOkUI(wxUpdateUIEvent &event) {
  event.Enable(m_ready && !m_textCtrlPattern->GetValue().empty() &&
               !m_textCtrlFolder->GetValue().Trim().empty());
}

// ---------------------------------------------------------------------------
// ripgrep on the machine
// ---------------------------------------------------------------------------

void FindInFilesDlg::CheckRipgrep() {
  m_ready = false;
  SetStatus(wxString::Format(_("Checking ripgrep on %s ..."),
                             m_account.GetDisplayName()));
  // Only looks: nothing is put on the machine before the user agrees.
  const Status started =
      m_installer.Start(m_account, ripgrep::Tool(), ripgrep::InstallDir(),
                        RemoteInstaller::Mode::kCheckOnly);
  if (!started.ok()) {
    SetStatus(started.message());
  }
}

void FindInFilesDlg::AskToInstall() {
  const ToolDescription &tool = ripgrep::Tool();
  const int answer = wxMessageBox(
      wxString::Format(_("%s %s is needed to search %s, and it is not "
                         "installed there (or it is another version).\n\n"
                         "Kennel will download it and put it in %s.\n\n"
                         "Install it now?"),
                       tool.name, tool.version, m_account.GetDisplayName(),
                       ripgrep::InstallDir()),
      wxString::Format(_("Download and Install %s?"), tool.name),
      wxYES_NO | wxICON_QUESTION, this);
  if (answer != wxYES) {
    // Nothing to search with: there is no point in keeping the dialog open.
    EndModal(wxID_CANCEL);
    return;
  }
  SetStatus(wxString::Format(_("Installing %s on %s ..."), tool.name,
                             m_account.GetDisplayName()));
  // From the handler of the end of the check, which is allowed.
  const Status started =
      m_installer.Start(m_account, tool, ripgrep::InstallDir());
  if (!started.ok()) {
    SetStatus(started.message());
  }
}

void FindInFilesDlg::OnInstallProgress(InstallEvent &event) {
  SetStatus(event.GetString());
}

void FindInFilesDlg::OnInstallDone(InstallEvent &event) {
  if (event.IsCancelled()) {
    return;
  }
  if (!event.GetResult().ok()) {
    SetStatus(event.GetResult().message());
    return;
  }
  if (event.NeedsInstall()) {
    AskToInstall();
    return;
  }
  m_ready = true;
  SetStatus(wxString::Format(
      event.WasAlreadyInstalled() ? _("ripgrep %s is on %s.")
                                  : _("ripgrep %s was installed on %s."),
      ripgrep::Tool().version, m_account.GetDisplayName()));
}

void FindInFilesDlg::SetStatus(const wxString &text) {
  m_staticTextStatus->SetLabel(text);
  m_staticTextStatus->Wrap(FromDIP(kStatusWrapDip));
  // A longer text makes the dialog taller; a shorter one leaves it as it is.
  GetSizer()->SetSizeHints(this);
  Layout();
}
