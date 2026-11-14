#include "MainView.hpp"

#include "ThemeLoader.h"
#include "ThemeManager.h"
#include "app/ActivityView.hpp"
#include "app/AssetBootstrap.h"
#include "app/FileTreeCtrl.hpp"
#include "app/OutputView.hpp"
#include "app/SettingsDlg.hpp"
#include "app/SideView.hpp"
#include "app/TabIcon.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Logger.h"
#include "core/Spinner.h"

#include <algorithm>
#include <vector>
#include <wx/aui/dockart.h>
#include <wx/dir.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/wupdlock.h>

namespace {
// The width the side panes start with when there is no saved layout, in DIP.
constexpr int kSidePaneWidthDip = 300;
// The size of a side pane that is dragged out of the window, in DIP. A saved
// layout has the size the user gave it.
constexpr int kFloatingPaneWidthDip = 400;
constexpr int kFloatingPaneHeightDip = 600;
// The names of the side panes (see MainViewBase).
const wxString kSidePanes[] = {"SideView", "Activity"};
// The pane of the results of Find in Files. It starts hidden, a search shows
// it, and it is hidden again when a saved layout is restored (see
// RestoreLayout()).
const wxString kOutputPane = "Output";
// The height the output pane gets when it is shown, in DIP.
constexpr int kOutputHeightDip = 400;
} // namespace

MainView::MainView(wxWindow *parent) : MainViewBase(parent) {
  const auto &prefs = AppManager::Get().GetPrefs();
  auto &themeMgr = ThemeManager::Get();
  {
    auto themes = LoadShippedThemes();
    if (themes.empty()) {
      KLOG_ERROR() << "Broken installation! can not find themes!";
      std::exit(1);
    }

    wxFont font;
    font.SetNativeFontInfo(prefs.terminalFontDesc);
    for (auto &t : themes) {
      t.theme.font = font;
    }

    themeMgr.Initialize(std::move(themes), prefs.terminalTheme);
    themeMgr.SetBlockCursor(prefs.blockCursor);
  }

  LoadBitmaps();

  m_sideView = new SideView(m_panelSideView);
  m_panelSideView->GetSizer()->Add(m_sideView, wxSizerFlags(1).Expand());
  m_activityView = new ActivityView(GetPanelActivity());
  GetPanelActivity()->GetSizer()->Add(m_activityView, wxSizerFlags(1).Expand());
  m_output = new OutputView(GetPanelOutput());
  GetPanelOutput()->GetSizer()->Add(m_output, wxSizerFlags(1).Expand());
  m_auimgr->GetPane(kOutputPane).Hide();
  m_auimgr->Update();

  // The size a pane gets when it floats (it only matters then, so no Update()).
  for (const wxString &name : kSidePanes) {
    m_auimgr->GetPane(name).FloatingSize(
        FromDIP(wxSize(kFloatingPaneWidthDip, kFloatingPaneHeightDip)));
  }
  m_defaultLayout = m_auimgr->SavePerspective();

  // The theme changed: the toolbars take its style.
  Bind(wxEVT_PAGES_TOOLBAR_STYLE, &MainView::OnToolBarStyle, this);
  // What the user double clicks in the file explorer.
  Bind(wxEVT_FILETREE_ACTIVATED, &MainView::OnFileTreeActivated, this);

  EventNotifier::Get()->Bind(wxEVT_APP_PREFS_CHANGED,
                             &MainView::OnAppPrefsSaved, this);
  EventNotifier::Get()->Bind(wxEVT_THEME_APPLIED, &MainView::OnThemeApplied,
                             this);
  ApplyCaptionColours();
  // Finalise frame construction.
  FinaliseConstruction();
}

MainView::~MainView() {
  EventNotifier::Get()->Unbind(wxEVT_APP_PREFS_CHANGED,
                               &MainView::OnAppPrefsSaved, this);
  EventNotifier::Get()->Unbind(wxEVT_THEME_APPLIED, &MainView::OnThemeApplied,
                               this);
}

void MainView::OnThemeApplied(wxCommandEvent &event) {
  event.Skip();
  ApplyCaptionColours();
  m_auimgr->Update();
  Refresh();
}

void MainView::ApplyCaptionColours() {
  const auto theme = ThemeManager::Get().ActiveTheme().value_or(
      wxSystemSettings::GetAppearance().IsDark()
          ? wxTerminalTheme::MakeDarkTheme()
          : wxTerminalTheme::MakeLightTheme());
  wxAuiDockArt *art = m_auimgr->GetArtProvider();
  // The background of the theme and its text. The caption has no gradient (see
  // UI.cpp), but the gradient colours are set too so that it can never show
  // another colour.
  art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, theme.bg);
  art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, theme.bg);
  art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, theme.fg);
  art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, theme.bg);
  art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, theme.bg);
  art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, theme.fg);
}

void MainView::BindFocusChildEvent(wxWindow *win, ChildFocusEvent handlerFn) {
  if (win == nullptr)
    return;
  win->Bind(wxEVT_CHILD_FOCUS, handlerFn, this);
  if (win->GetChildren().empty())
    return;
  for (auto iter = win->GetChildren().begin(); iter != win->GetChildren().end();
       ++iter) {
    BindFocusChildEvent(*iter, handlerFn);
  }
}

void MainView::FinaliseConstruction() {
  BindFocusChildEvent(m_panelActivity, &MainView::OnActivityPanelChildFocus);
  BindFocusChildEvent(m_panelSideView,
                      &MainView::OnFileExplorerPanelChildFocus);
  BindFocusChildEvent(m_mainPanel, &MainView::OnMainPanelChildFocus);
  ApplyPrefs();
}

// ---------------------------------------------------------------------------
// The theme and the file explorer
// ---------------------------------------------------------------------------

void MainView::OnToolBarStyle(wxCommandEvent &event) {
  event.Skip(); // The main toolbar of the frame follows too
  m_sideView->ApplyToolBarStyle();
  m_output->ApplyToolBarStyle();
}

void MainView::OnFileTreeActivated(FileEvent &event) {
  EventNotifier::Get()->AddPendingEvent(event);
}

// ---------------------------------------------------------------------------
// The left pane
// ---------------------------------------------------------------------------

void MainView::RestoreLayout() {
  // Deferred: the docks can only get the size they had where the window is big
  // enough for them, and the window is not at its final size (e.g. maximized)
  // before the first pass of the event loop.
  CallAfter([this] {
    const wxString &layout = AppManager::Get().GetPrefs().auiLayout;
    const int sideTab = AppManager::Get().GetPrefs().sideTab;
    m_sideView->SetSelection(sideTab);
    bool restored = false;
    if (!layout.empty()) {
      // No update yet: the pane of the results is hidden below, before the
      // window shows the layout.
      restored = m_auimgr->LoadPerspective(layout, /*update=*/false);
      if (!restored) {
        KLOG_WARN() << "Could not restore the layout of the panes";
      }
    }
    if (restored) {
      // LoadPerspective() hides the panes the layout does not know: a layout
      // saved before a pane existed. They are shown where they were created,
      // with the default width if they are side panes. The check relies on the
      // format of the AUI perspective ("name=<pane>;" for each pane).
      std::vector<wxString> added;
      // The results of a search are not kept between runs, so the pane of them
      // does not come back, whether the layout was saved with it showing or
      // does not know it: the next search shows it.
      m_auimgr->GetPane(kOutputPane).Hide();
      wxAuiPaneInfoArray &panes = m_auimgr->GetAllPanes();
      for (size_t i = 0; i < panes.GetCount(); ++i) {
        if (panes[i].name == kOutputPane) {
          continue;
        }
        if (!layout.Contains("name=" + panes[i].name + ";")) {
          panes[i].Show();
          if (std::find(std::begin(kSidePanes), std::end(kSidePanes),
                        panes[i].name) != std::end(kSidePanes)) {
            added.push_back(panes[i].name);
          }
        }
      }
      m_auimgr->Update();
      if (!added.empty()) {
        ApplyDefaultPaneWidths(added);
      }
    } else {
      ApplyDefaultPaneWidths(kSidePanes);
    }
    m_layoutRestored = true;
  });
}

void MainView::ResetLayout() {
  if (!m_auimgr->LoadPerspective(m_defaultLayout, /*update=*/true)) {
    KLOG_WARN() << "Could not reset the layout of the panes";
    return;
  }
  ApplyDefaultPaneWidths(kSidePanes);
}

bool MainView::IsPaneShown(const wxString &name) const {
  return m_auimgr->GetPane(name).IsShown();
}

void MainView::ShowPane(const wxString &name, bool show) {
  wxAuiPaneInfo &pane = m_auimgr->GetPane(name);
  if (!pane.IsOk() || pane.IsShown() == show) {
    return;
  }
  pane.Show(show);
  if (show && name == kOutputPane) {
    // The output is a list: it needs height. A dock keeps the size it got
    // when it was created, so a minimum size makes it that tall, and is dropped
    // again to let the user make it smaller. Not when it is that tall already.
    const int height = FromDIP(kOutputHeightDip);
    if (pane.rect.GetHeight() < height) {
      pane.BestSize(-1, height).MinSize(-1, height);
      m_auimgr->Update();
      pane.MinSize(wxDefaultSize);
    }
  }
  m_auimgr->Update();
}

void MainView::ToggleAllPanes() {
  wxWindowUpdateLocker locker{this};
  std::vector<wxString> shown;
  for (const wxString &name : kSidePanes) {
    if (IsPaneShown(name)) {
      shown.push_back(name);
    }
  }
  if (!shown.empty()) {
    for (const wxString &name : shown) {
      m_auimgr->GetPane(name).Show(false);
    }
    m_hiddenByToggle = std::move(shown);
  } else {
    if (m_hiddenByToggle.empty()) {
      m_hiddenByToggle.assign(std::begin(kSidePanes), std::end(kSidePanes));
    }
    for (const wxString &name : m_hiddenByToggle) {
      m_auimgr->GetPane(name).Show(true);
    }
    m_hiddenByToggle.clear();
  }
  m_auimgr->Update();
}

void MainView::ApplyDefaultPaneWidths(std::span<const wxString> names) {
  // A dock keeps the size it got when it was created (and a new dock is at most
  // 30% of the window wide), so BestSize() cannot change it any more. A minimum
  // size makes the dock at least that wide; it is dropped again so the user can
  // make the pane narrower.
  const int width = FromDIP(kSidePaneWidthDip);
  for (const wxString &name : names) {
    m_auimgr->GetPane(name).BestSize(width, -1).MinSize(width, -1);
  }
  m_auimgr->Update();
  for (const wxString &name : names) {
    m_auimgr->GetPane(name).MinSize(wxDefaultSize);
  }
  m_auimgr->Update();
}

void MainView::SaveLayout() {
  // Before the saved layout was applied the panes still hold their defaults,
  // which must not replace what was saved (see RestoreLayout()).
  if (m_layoutRestored) {
    AppManager::Get().GetPrefs().auiLayout = m_auimgr->SavePerspective();
    AppManager::Get().GetPrefs().sideTab = m_sideView->GetSelection();
  }
}

void MainView::LoadBitmaps() {
  auto &bmps = AppManager::Get().GetBitmaps();
  bmps.Load("home.svg");
  bmps.AddAlias("home.svg", "home");

  bmps.Load("folder.svg");
  bmps.AddAlias("folder.svg", "folder");

  bmps.Load("folder-open.svg");
  bmps.AddAlias("folder-open.svg", "folder-open");

  bmps.Load("up.svg");
  bmps.AddAlias("up.svg", "up");

  // The status of the activity view.
  bmps.Load("hand.svg");
  bmps.AddAlias("hand.svg", "hand");

  bmps.Load("check.svg");
  bmps.AddAlias("check.svg", "check");

  for (int i = 0; i < Spinner::kFrameCount; ++i) {
    const wxString file = wxString::Format("spinner-%d.svg", i);
    bmps.Load(file);
    bmps.AddAlias(file, wxString::Format("spinner-%d", i));
    const wxString pulse = wxString::Format("blocked-%d.svg", i);
    bmps.Load(pulse);
    bmps.AddAlias(pulse, wxString::Format("blocked-%d", i));
  }

  // The git view: the icon of its tab (the same for both appearances) and the
  // status of a file.
  for (const char *name :
       {"git", "git-new", "git-deleted", "git-modified", "git-add",
        "git-commit", "git-push", "git-pull", "git-log", "git-reset",
        "git-renamed", "git-conflict", "git-untracked"}) {
    const wxString file = wxString(name) + ".svg";
    bmps.Load(file);
    bmps.AddAlias(file, name);
  }

  bmps.Load("terminal.svg");
  bmps.AddAlias("terminal.svg", "terminal");

  bmps.Load("terminals.svg");
  bmps.AddAlias("terminals.svg", "terminals");

  bmps.Load("list-view.svg");
  bmps.AddAlias("list-view.svg", "list-view");

  bmps.Load("restart.svg");
  bmps.AddAlias("restart.svg", "restart");

  bmps.Load("new.svg");
  bmps.AddAlias("new.svg", "new");

  bmps.Load("agent.svg");
  bmps.AddAlias("agent.svg", "agent");

  bmps.Load("job.svg");
  bmps.AddAlias("job.svg", "job");

  bmps.Load("archive.svg");
  bmps.AddAlias("archive.svg", "archive");

  bmps.Load("pin.svg");
  bmps.AddAlias("pin.svg", "pin");

  bmps.Load("save.svg");
  bmps.AddAlias("save.svg", "save");

  bmps.Load("reload.svg");
  bmps.AddAlias("reload.svg", "reload");

  bmps.Load("wrap.svg");
  bmps.AddAlias("wrap.svg", "wrap");

  bmps.Load("close.svg");
  bmps.AddAlias("close.svg", "close");

  bmps.Load("copy.svg");
  bmps.AddAlias("copy.svg", "copy");

  bmps.Load("paste.svg");
  bmps.AddAlias("paste.svg", "paste");

  bmps.Load("find.svg");
  bmps.AddAlias("find.svg", "find");

  bmps.Load("stop.svg");
  bmps.AddAlias("stop.svg", "stop");

  bmps.Load("clear.svg");
  bmps.AddAlias("clear.svg", "clear");

  bmps.Load("code-reviewer.svg");
  bmps.AddAlias("code-reviewer.svg", "code-reviewer");

  bmps.LoadMimeIcons();

  // Load file*.svg
  wxArrayString files;
  wxDir::GetAllFiles(ShippedAssetsDir().GetPath(), &files, "file*.svg",
                     wxDIR_FILES);
  for (const wxString &file : files) {
    wxFileName fn{file};
    bmps.Load(fn.GetFullName());
    bmps.AddAlias(fn.GetFullName(), fn.GetName());
  }

  const auto &agents = AppManager::Get().Adapters().Agents();
  for (const auto &agent : agents) {
    if (wxFileExists(agent.iconPath)) {
      bmps.Load(agent.iconPath);
      // Alias by agent name so a session leaf can be restored to its
      // agent's icon (see SetAgentIcon).
      bmps.AddAlias(agent.iconPath, agent.name);
    }
  }
}

bool MainView::MakeActive(wxWindow *win) {
  bool needRefresh{false};
  for (wxAuiPaneInfo &pane : m_auimgr->GetAllPanes()) {
    if (pane.window == win) {
      needRefresh = (pane.state & wxAuiPaneInfo::optionActive) == 0;
      pane.state |= wxAuiPaneInfo::optionActive;
    } else {
      pane.state &= ~wxAuiPaneInfo::optionActive;
    }
  }
  return needRefresh;
}

void MainView::OnMainPanelChildFocus(wxChildFocusEvent &event) {
  event.Skip();
  if (MakeActive(m_mainPanel))
    m_auimgr->Update();
}

void MainView::OnActivityPanelChildFocus(wxChildFocusEvent &event) {
  event.Skip();
  if (MakeActive(m_panelActivity))
    m_auimgr->Update();
}

void MainView::OnFileExplorerPanelChildFocus(wxChildFocusEvent &event) {
  event.Skip();
  if (MakeActive(m_panelSideView))
    m_auimgr->Update();
}

void MainView::OnAppPrefsSaved(wxCommandEvent &event) {
  event.Skip();
  wxWindowUpdateLocker locker{this};
  ApplyPrefs();
}

void MainView::ApplyPrefs() {
  m_sideView->ApplyPrefs();
  m_output->ApplyPrefs();
}
