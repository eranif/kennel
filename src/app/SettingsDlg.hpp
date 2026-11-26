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
  bool GetGroupTabs() const { return m_checkBoxGroupTabs->IsChecked(); }
  bool GetFileTabShowsFolder() const {
    return m_checkBoxFileTabShowsFolder->IsChecked();
  }
  wxDirection GetTabPosition() const;
  TabHeight GetTabHeight() const;
  wxDirection GetSideBarTabPosition() const;
  wxString GetTabOutlineColour() const {
    return m_choiceTabOutlineColour->GetStringSelection();
  }

  wxString GetShell() const { return m_choiceShell->GetStringSelection(); }
  wxString GetDefaultHomeDir() const {
    return m_textCtrlShellWorkingDir->GetValue();
  }
  bool OptimizeTerminalDrawing() const {
    return m_checkBoxOptimizeDrawings->IsChecked();
  }
  bool IsCloseButtonOnTab() const {
    return m_checkBoxCloseButtonOnTab->IsChecked();
  }

  bool GetCheckForUpdatesOnStartup() const {
    return m_checkBoxForNewVersionOnStartup->IsChecked();
  }

  size_t GetScrollBackLines() const {
    return m_spinCtrlScrollBackLines->GetValue();
  }

  // The "Tmux" page: whether new sessions run in tmux, and the tmux commands
  // (one per line) that are set for the sessions of agents and of terminals.
  bool GetTmuxEnabled() const { return m_checkBoxTmuxEnabled->IsChecked(); }
  wxString GetTmuxAgentSettings() const {
    return m_textCtrlTmuxAgentSettings->GetValue();
  }
  wxString GetTmuxTerminalSettings() const {
    return m_textCtrlTmuxTerminalSettings->GetValue();
  }

  static wxColour GetTabOutlineColourOf(const wxTerminalTheme &theme,
                                        const wxString &key);
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
  // Puts the tab height that was saved back (when the dialog is cancelled).
  void RestoreTabHeight();
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
  void OnTabOutlineColour(wxCommandEvent &event) override;
  void OnTabPosition(wxCommandEvent &event) override;
  void OnBrowseForShellWorkingDir(wxCommandEvent &event) override;
  void OnChoiceTheme(wxCommandEvent &event) override;
  void OnFontSelected(wxFontPickerEvent &event) override;
  // The "Answers" page: Remove takes back the answer that is selected in the
  // list, Remove All all of them (the questions are asked again).
  void OnRemoveAnswer(wxCommandEvent &event) override;
  void OnRemoveAllAnswers(wxCommandEvent &event) override;
  // Both are disabled when the list has no answers.
  void OnRemoveAnswerUI(wxUpdateUIEvent &event) override;
  void OnRemoveAllAnswersUI(wxUpdateUIEvent &event) override;

  // Shows the stored answers (DialogAnswers) in the list of the "Answers" page.
  void ShowAnswers();

  // Not in the generated dialog: bound in the constructor. They put the
  // default tmux commands back in the text of the page.
  void OnResetTmuxAgentSettings(wxCommandEvent &event);
  void OnResetTmuxTerminalSettings(wxCommandEvent &event);

  // Tells the tab bars (the notebook and the side bar) to show a tab height.
  static void SendTabHeight(TabHeight height);
  void PreviewTabHeight();

  wxFont m_initialFont{wxNullFont};
  wxString m_initialTheme;
};
