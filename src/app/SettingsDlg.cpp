#include "SettingsDlg.hpp"
#include "app/FileBrowserDlg.hpp"
#include "app/MainFrame.h"
#include "app/ThemeManager.h"
#include "core/AppManager.h"
#include "core/Helpers.h"
#include "terminal_view.h"
#include <wx/tokenzr.h>

/// A theme colour that the outline of the current tab can have.
struct TabOutlineColour {
  const char *label; ///< The name in the settings dialog.
  wxColour wxTerminalTheme::*member;
};

const std::vector<TabOutlineColour> &GetTabOutlineColours() {
  static const std::vector<TabOutlineColour> colours = {
      {"Text", &wxTerminalTheme::fg},
      {"Red", &wxTerminalTheme::red},
      {"Green", &wxTerminalTheme::green},
      {"Yellow", &wxTerminalTheme::yellow},
      {"Blue", &wxTerminalTheme::blue},
      {"Magenta", &wxTerminalTheme::magenta},
      {"Cyan", &wxTerminalTheme::cyan},
      {"White", &wxTerminalTheme::white},
      {"Bright Red", &wxTerminalTheme::brightRed},
      {"Bright Green", &wxTerminalTheme::brightGreen},
      {"Bright Yellow", &wxTerminalTheme::brightYellow},
      {"Bright Blue", &wxTerminalTheme::brightBlue},
      {"Bright Magenta", &wxTerminalTheme::brightMagenta},
      {"Bright Cyan", &wxTerminalTheme::brightCyan},
      {"Bright White", &wxTerminalTheme::brightWhite},
      {"Selection", &wxTerminalTheme::selectionBg},
      {"Search Match", &wxTerminalTheme::highlightBg},
      {"Cursor", &wxTerminalTheme::cursorColour},
      {"Link", &wxTerminalTheme::linkColour},
  };
  return colours;
}

wxColour SettingsDlg::GetTabOutlineColourOf(const wxTerminalTheme &theme,
                                            const wxString &key) {
  wxColour colour = theme.selectionBg;
  for (const auto &entry : GetTabOutlineColours()) {
    if (key == entry.label) {
      colour = theme.*(entry.member);
      break;
    }
  }

  // Some theme colours have alpha; the outline is solid.
  return wxColour{colour.Red(), colour.Green(), colour.Blue()};
}

wxArrayString GetTabOutlineColoursArray() {
  wxArrayString colours;
  for (const auto &entry : GetTabOutlineColours()) {
    colours.Add(entry.label);
  }
  return colours;
}

SettingsDlg::SettingsDlg(wxWindow *parent) : SettingsDlgBase(parent) {
  const auto &prefs = AppManager::Get().GetPrefs();

  m_initialFont.SetNativeFontInfo(prefs.terminalFontDesc);
  m_fontPicker->SetSelectedFont(m_initialFont);

  wxString tabPos{"Left"};
  switch (prefs.tabPosition) {
  case wxTOP:
    tabPos = "Top";
    break;
  case wxRIGHT:
    tabPos = "Right";
    break;
  case wxBOTTOM:
    tabPos = "Bottom";
    break;
  default:
  case wxLEFT:
    tabPos = "Left";
    break;
  }
  m_choiceTabPosition->SetStringSelection(tabPos);
  m_checkBoxCloseButtonOnTab->SetValue(prefs.tabHasCloseButton);
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
  // Not in the generated dialog: it is added here (the last choice).
  m_choiceAppTheme->Append(_("Follow terminal theme"));
  if (prefs.appTheme == kAppThemeFollowTerminal) {
    m_choiceAppTheme->SetSelection(kChoiceFollowTerminal);
  } else {
    switch (ParseAppTheme(prefs.appTheme)) {
    case wxApp::Appearance::Dark:
      m_choiceAppTheme->SetSelection(1);
      break;
    case wxApp::Appearance::Light:
      m_choiceAppTheme->SetSelection(2);
      break;
    case wxApp::Appearance::System:
      m_choiceAppTheme->SetSelection(0);
      break;
    }
  }

  m_choiceTabOutlineColour->Append(GetTabOutlineColoursArray());
  m_choiceTabOutlineColour->SetStringSelection(prefs.tabOutlineColour);

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
    GetMainFrame()->GetMainView()->GetSessionsBook()->ApplyTheme(themeName);
}

void SettingsDlg::OnFontSelected(wxFontPickerEvent &event) {
  GetMainFrame()->GetMainView()->GetSessionsBook()->ApplyFont(event.GetFont());
}

void SettingsDlg::RestoreThemeAndFont() {
  if (m_initialFont.IsOk())
    GetMainFrame()->GetMainView()->GetSessionsBook()->ApplyFont(m_initialFont);
  if (!m_initialTheme.empty())
    GetMainFrame()->GetMainView()->GetSessionsBook()->ApplyTheme(
        m_initialTheme);
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

void SettingsDlg::OnTabPosition(wxCommandEvent &event) {
  wxUnusedVar(event);
  // Apply the changes for a preview
}

void SettingsDlg::OnTabOutlineColour(wxCommandEvent &event) {
  wxUnusedVar(event);
  // Apply the changes for a preview
}

wxDirection SettingsDlg::GetTabPosition() const {
  wxString pos = m_choiceTabPosition->GetStringSelection();
  if (pos == "Top") {
    return wxDirection::wxTOP;
  } else if (pos == "Bottom") {
    return wxDirection::wxBOTTOM;
  } else if (pos == "Right") {
    return wxDirection::wxRIGHT;
  } else {
    return wxDirection::wxLEFT;
  }
}