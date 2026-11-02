#include "app/GitCommitMessageDlg.hpp"
#include "app/Editor.h"
#include "app/ThemeManager.h"

GitCommitMessageDlg::GitCommitMessageDlg(wxWindow *parent)
    : GitCommitMessageDlgBase(parent) {
  const bool isDark = wxSystemSettings::GetAppearance().IsDark();
  auto theme = ThemeManager::Get().ActiveTheme().value_or(
      isDark ? wxTerminalTheme::MakeDarkTheme()
             : wxTerminalTheme::MakeLightTheme());
  Editor::InitEditor(m_stcCommitMessage, EditorLang::kMarkdown, theme);
}

GitCommitMessageDlg::~GitCommitMessageDlg() {}
