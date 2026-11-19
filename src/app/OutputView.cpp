#include "app/OutputView.hpp"

#include "GlyphTabArt.h"
#include "ThemeManager.h"
#include "app/FindResultsView.hpp"
#include "app/GitPanel.hpp"
#include "app/SettingsDlg.hpp"
#include "app/TabIcon.h"
#include "app/TabPadding.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"

#include <wx/aui/auibook.h>
#include <wx/settings.h>
#include <wx/sizer.h>

OutputView::OutputView(wxWindow *parent)
    : OutputViewBase(parent),
      m_gitTerminals(std::make_unique<GitTerminals>(m_bookGitTerminals)) {
  if (const int page = m_auiOutputBook->FindPage(m_pageGit);
      page != wxNOT_FOUND) {
    m_auiOutputBook->SetPageBitmap(
        page, TabIcon(AppManager::Get().GetBitmaps().GetByAlias("git", false)));
  }
  m_findInFiles = new FindResultsView(m_panelFindInFiles);
  m_panelFindInFiles->GetSizer()->Add(m_findInFiles, wxSizerFlags(1).Expand());
  ApplyPrefs();

  auto *notifier = EventNotifier::Get();
  notifier->Bind(wxEVT_APP_TAB_HEIGHT, &OutputView::OnTabHeight, this);
  notifier->Bind(wxEVT_THEME_APPLIED, &OutputView::OnThemeApplied, this);
  notifier->Bind(wxEVT_GITVIEW_RUN_IN_TERMINAL,
                 &OutputView::OnGitViewRunInTerminal, this);
  notifier->Bind(wxEVT_GITVIEW_SHOWN, &OutputView::OnGitViewShown, this);
  notifier->Bind(wxEVT_GITVIEW_CLOSED, &OutputView::OnGitViewClosed, this);
}

OutputView::~OutputView() {
  auto *notifier = EventNotifier::Get();
  notifier->Unbind(wxEVT_APP_TAB_HEIGHT, &OutputView::OnTabHeight, this);
  notifier->Unbind(wxEVT_THEME_APPLIED, &OutputView::OnThemeApplied, this);
  notifier->Unbind(wxEVT_GITVIEW_RUN_IN_TERMINAL,
                   &OutputView::OnGitViewRunInTerminal, this);
  notifier->Unbind(wxEVT_GITVIEW_SHOWN, &OutputView::OnGitViewShown, this);
  notifier->Unbind(wxEVT_GITVIEW_CLOSED, &OutputView::OnGitViewClosed, this);
}

void OutputView::SelectTab(Tab tab) {
  wxWindow *page = tab == Tab::kGit ? m_pageGit : m_bookPageFindInFiles;
  if (const int index = m_auiOutputBook->FindPage(page);
      index != wxNOT_FOUND && index != m_auiOutputBook->GetSelection()) {
    m_auiOutputBook->SetSelection(index);
  }
}

void OutputView::ApplyToolBarStyle() { m_findInFiles->ApplyToolBarStyle(); }

void OutputView::ApplyPrefs() {
  const auto &prefs = AppManager::Get().GetPrefs();
  const auto theme = ThemeManager::Get().ActiveTheme().value_or(
      wxSystemSettings::GetAppearance().IsDark()
          ? wxTerminalTheme::MakeDarkTheme()
          : wxTerminalTheme::MakeLightTheme());
  // The art does not follow the theme by itself: a new one for each change. The
  // notebook takes it and owns it.
  auto *art = new GlyphTabArt(theme, GlyphTabPainter::Style::Browser);
  art->SetOutlineColour(
      SettingsDlg::GetTabOutlineColourOf(theme, prefs.tabOutlineColour));
  art->SetPaddingY(TabPaddingOf(prefs.tabHeight));
  m_auiOutputBook->SetArtProvider(art);
  m_auiOutputBook->Refresh();
}

void OutputView::OnThemeApplied(wxCommandEvent &event) {
  event.Skip();
  ApplyPrefs();
}

void OutputView::OnTabHeight(wxCommandEvent &event) {
  event.Skip(); // The other notebooks show it too
  if (auto *art =
          dynamic_cast<GlyphTabArt *>(m_auiOutputBook->GetArtProvider())) {
    art->SetPaddingY(TabPaddingOf(static_cast<TabHeight>(event.GetInt())));
    m_auiOutputBook->Refresh();
  }
}

void OutputView::OnGitViewRunInTerminal(FileEvent &event) {
  event.Skip(); // The host shows the pane
  SelectTab(Tab::kGit);
  m_gitTerminals->Run(event.GetInt(), event.GetAccount(), event.GetPath(),
                      event.GetString());
}

void OutputView::OnGitViewShown(FileEvent &event) {
  event.Skip();
  m_gitTerminals->Show(event.GetInt());
}

void OutputView::OnGitViewClosed(FileEvent &event) {
  event.Skip();
  m_gitTerminals->Remove(event.GetInt());
}
