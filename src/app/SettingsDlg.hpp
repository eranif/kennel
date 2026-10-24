#pragma once

#include "UI.hpp"
#include "app/AppTheme.h"
#include <wx/app.h>

class SettingsDlg : public SettingsDlgBase {
public:
  SettingsDlg(wxWindow *parent);
  ~SettingsDlg() override;

  wxString GetTheme() const { return m_choiceTheme->GetStringSelection(); }
  wxString GetDefaultLoginShell() const {
    wxStringClientData *cd = dynamic_cast<wxStringClientData *>(
        m_choiceShell->GetClientObject(m_choiceShell->GetSelection()));
    return cd->GetData();
  }
  wxFont GetSelectedFont() const { return m_fontPicker->GetSelectedFont(); }
  bool GetUseBlockCursor() const { return m_checkBoxBlockCaret->IsChecked(); }
  wxString GetShell() const { return m_choiceShell->GetStringSelection(); }
  wxString GetDefaultHomeDir() const {
    return m_textCtrlShellWorkingDir->GetValue();
  }
  bool OptimizeTerminalDrawing() const {
    return m_checkBoxOptimizeDrawings->IsChecked();
  }
  bool GetCheckForUpdatesOnStartup() const {
    return m_checkBoxForNewVersionOnStartup->IsChecked();
  }

  size_t GetScrollBackLines() const {
    return m_spinCtrlScrollBackLines->GetValue();
  }

  // The index of the "Follow terminal theme" choice of the application theme.
  static constexpr int kChoiceFollowTerminal = 3;

  // "system", "dark", "light" or "terminal", as saved in the preferences.
  wxString GetAppThemeName() const {
    if (m_choiceAppTheme->GetSelection() == kChoiceFollowTerminal) {
      return kAppThemeFollowTerminal;
    }
    return AppThemeName(GetAppTheme());
  }
  void RestoreThemeAndFont();
  wxApp::Appearance GetAppTheme() const {
    switch (m_choiceAppTheme->GetSelection()) {
    case 1:
      return wxApp::Appearance::Dark;
    case 2:
      return wxApp::Appearance::Light;
    case 0:
    default:
      return wxApp::Appearance::System;
    }
  }

protected:
  void OnBrowseForShellWorkingDir(wxCommandEvent &event) override;
  void OnChoiceTheme(wxCommandEvent &event) override;
  void OnFontSelected(wxFontPickerEvent &event) override;

  wxFont m_initialFont{wxNullFont};
  wxString m_initialTheme;
};
