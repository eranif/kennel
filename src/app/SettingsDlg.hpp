#pragma once

#include "UI.hpp"

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

  void RestoreThemeAndFont();

protected:
  void OnBrowseForShellWorkingDir(wxCommandEvent &event) override;
  void OnChoiceTheme(wxCommandEvent &event) override;
  void OnFontSelected(wxFontPickerEvent &event) override;

  wxFont m_initialFont{wxNullFont};
  wxString m_initialTheme;
};
