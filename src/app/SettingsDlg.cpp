#include "SettingsDlg.hpp"
#include "app/ThemeManager.h"
#include "core/AppManager.h"
#include "core/Helpers.h"
#include "terminal_view.h"
#include <wx/tokenzr.h>

SettingsDlg::SettingsDlg(wxWindow *parent) : SettingsDlgBase(parent) {
  const auto &prefs = AppManager::Get().GetPrefs();

  wxFont f;
  f.SetNativeFontInfo(prefs.terminalFontDesc);
  m_fontPicker->SetSelectedFont(f);

  m_choiceTheme->Append(ThemeManager::Get().Names());
  int where = m_choiceTheme->FindString(prefs.terminalTheme);
  if (where == wxNOT_FOUND && !ThemeManager::Get().Themes().empty()) {
    where = 0;
  }

  if (where != wxNOT_FOUND) {
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
  m_dirPickerShellHomeDir->SetPath(prefs.terminalHomeDir);
  GetSizer()->Fit(this);
  PositionDialog(this);
}

SettingsDlg::~SettingsDlg() {}
