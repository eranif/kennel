#include "FindInFilesDlg.hpp"

#include "core/Ripgrep.h"

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
  if (!ripgrep::IsInstalledByKennel(m_account)) {
    // This machine on Windows or macOS: the copy that comes with Kennel.
    m_ready = !ripgrep::ExecutableOf(m_account).empty();
    SetStatus(m_ready ? wxString::Format(_("ripgrep %s is ready."),
                                         ripgrep::Tool().version)
                      : _("ripgrep (rg) was not found next to Kennel: it needs "
                          "to be installed again."));
    return;
  }

  SetStatus(wxString::Format(_("Checking ripgrep on %s ..."),
                             m_account.GetDisplayName()));
  const Status started =
      m_installer.Start(m_account, ripgrep::Tool(), ripgrep::LinuxInstallDir());
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
