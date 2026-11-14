#include "app/OutputView.hpp"

#include "GlyphTabArt.h"
#include "app/FindResultsView.hpp"
#include "app/SettingsDlg.hpp"
#include "app/TabPadding.h"
#include "app/ThemeManager.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"

#include <wx/aui/auibook.h>
#include <wx/settings.h>
#include <wx/sizer.h>

const wxString OutputView::kFindInFilesTab = "Find in Files";

OutputView::OutputView(wxWindow *parent) : wxPanel(parent, wxID_ANY) {
  m_notebook =
      new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                        wxAUI_NB_TOP | wxAUI_NB_SCROLL_BUTTONS | wxBORDER_NONE);
  m_findInFiles = new FindResultsView(m_notebook);
  m_notebook->AddPage(m_findInFiles, kFindInFilesTab, /*select=*/true);

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_notebook, wxSizerFlags(1).Expand());
  SetSizer(sizer);
  ApplyPrefs();

  EventNotifier::Get()->Bind(wxEVT_THEME_APPLIED, &OutputView::OnThemeApplied,
                             this);
  EventNotifier::Get()->Bind(wxEVT_APP_TAB_HEIGHT, &OutputView::OnTabHeight,
                             this);
}

OutputView::~OutputView() {
  EventNotifier::Get()->Unbind(wxEVT_THEME_APPLIED, &OutputView::OnThemeApplied,
                               this);
  EventNotifier::Get()->Unbind(wxEVT_APP_TAB_HEIGHT, &OutputView::OnTabHeight,
                               this);
}

void OutputView::SelectFindInFiles() { SelectTab(kFindInFilesTab); }

bool OutputView::SelectTab(const wxString &name) {
  for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
    if (m_notebook->GetPageText(i) == name) {
      m_notebook->SetSelection(i);
      return true;
    }
  }
  return false;
}

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
  m_notebook->SetArtProvider(art);
  m_notebook->Refresh();
}

void OutputView::ApplyToolBarStyle() { m_findInFiles->ApplyToolBarStyle(); }

void OutputView::OnThemeApplied(wxCommandEvent &event) {
  event.Skip();
  ApplyPrefs();
}

void OutputView::OnTabHeight(wxCommandEvent &event) {
  event.Skip(); // The other books show it too
  if (auto *art = dynamic_cast<GlyphTabArt *>(m_notebook->GetArtProvider())) {
    art->SetPaddingY(TabPaddingOf(static_cast<TabHeight>(event.GetInt())));
    m_notebook->Refresh();
  }
}
