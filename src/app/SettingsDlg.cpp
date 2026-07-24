#include "SettingsDlg.hpp"
#include "app/FileBrowserDlg.hpp"
#include "app/MainFrame.h"
#include "app/ThemeManager.h"
#include "core/AppManager.h"
#include "core/Helpers.h"
#include "terminal_view.h"
#include <wx/tokenzr.h>

SettingsDlg::SettingsDlg(wxWindow *parent) : SettingsDlgBase(parent) {
  const auto &prefs = AppManager::Get().GetPrefs();

  m_initialFont.SetNativeFontInfo(prefs.terminalFontDesc);
  m_fontPicker->SetSelectedFont(m_initialFont);

  m_choiceTheme->Append(ThemeManager::Get().Names());
  int where = m_choiceTheme->FindString(prefs.terminalTheme);
  if (where == wxNOT_FOUND && !ThemeManager::Get().Themes().empty()) {
    where = 0;
  }

  if (where != wxNOT_FOUND) {
    m_initialTheme = prefs.terminalTheme;
    m_choiceTheme->SetSelection(where);
  }

  if (wxTerminalViewCtrl::IsOpenGLEnabled()) {
    m_checkBoxOptimizeDrawings->SetValue(false);
    m_checkBoxOptimizeDrawings->Enable(false);
  } else {
    m_checkBoxOptimizeDrawings->SetValue(prefs.terminalOptimizedDrawing);
  }

  auto result = ::FindShells();
  int selection{0};
  for (const auto &[shell_name, shell_cmd] : result.shells) {
    if (prefs.terminalLoginShell == shell_cmd) {
      selection = m_choiceShell->GetCount();
    }
    m_choiceShell->Append(shell_name, new wxStringClientData(shell_cmd));
  }
  m_choiceShell->SetSelection(selection);
  m_checkBoxBlockCaret->SetValue(prefs.blockCursor);
  m_checkBoxForNewVersionOnStartup->SetValue(prefs.checkForUpdatesOnStartup);
  m_spinCtrlScrollBackLines->SetValue(prefs.scrollbackLines);
  m_textCtrlShellWorkingDir->SetValue(prefs.terminalHomeDir);
  GetSizer()->Fit(this);
  PositionDialog(this);
}

SettingsDlg::~SettingsDlg() {}

void SettingsDlg::OnChoiceTheme(wxCommandEvent &event) {
  int selection = event.GetSelection();
  wxString themeName = m_choiceTheme->GetString(selection);
  if (!themeName.empty())
    GetMainFrame()->GetMainView()->ApplyTheme(themeName);
}

void SettingsDlg::OnFontSelected(wxFontPickerEvent &event) {
  GetMainFrame()->GetMainView()->ApplyFont(event.GetFont());
}

void SettingsDlg::RestoreThemeAndFont() {
  if (m_initialFont.IsOk())
    GetMainFrame()->GetMainView()->ApplyFont(m_initialFont);
  if (!m_initialTheme.empty())
    GetMainFrame()->GetMainView()->ApplyTheme(m_initialTheme);
}

void SettingsDlg::OnBrowseForShellWorkingDir(wxCommandEvent &event) {
  wxUnusedVar(event);
  std::unique_ptr<FileProvider> provider{nullptr};
  if (m_choiceShell->GetStringSelection().StartsWith("WSL:")) {
    wxString distroName = m_choiceShell->GetStringSelection().Mid(4);
    distroName.Trim().Trim(false);
    provider =
        std::make_unique<WSLFilesProvider>(distroName, wxEmptyString, true);
  } else {
    provider = std::make_unique<LocalFilesProvider>(wxEmptyString, true);
  }
  FileBrowserDlg dlg(this, std::move(provider));
  if (dlg.ShowModal() == wxID_OK) {
    m_textCtrlShellWorkingDir->ChangeValue(dlg.GetPath());
  }
}
