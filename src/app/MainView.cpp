#include "MainView.hpp"

#include "ThemeLoader.h"
#include "ThemeManager.h"
#include "app/AssetBootstrap.h"
#include "app/FileTreeCtrl.hpp"
#include "app/FindResultsView.hpp"
#include "app/GitPanel.hpp"
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
// The width the side bar starts with when there is no saved layout: a quarter
// of the window.
constexpr int kSidePaneWidthPercent = 25;
// The name of the pane of the side bar (see MainViewBase). The side bar has
// the views as tabs: the file explorer, git and the activity.
const wxString kSidePane = "SideView";
// The name of the pane of the Output View (see MainViewBase).
const wxString kOutputViewPane = "OutputView";
// The panes that an older version had: a layout that has them was saved by it,
// and is not applied (see RestoreLayout()).
const wxString kRemovedPanes[] = {"Activity", "Output"};
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
  m_outputView = new OutputView(m_panelOutputView);
  m_panelOutputView->GetSizer()->Add(m_outputView, wxSizerFlags(1).Expand());
  m_panelSideView->GetSizer()->Add(m_sideView, wxSizerFlags(1).Expand());

  // The side bar has no caption to drag it by: it stays docked, left or right.
  m_auimgr->GetPane(kSidePane).Floatable(false);
  // The Output View shows when something writes to it (a git command).
  m_auimgr->GetPane(kOutputViewPane).Hide();
  m_auimgr->Update();
  m_defaultLayout = m_auimgr->SavePerspective();

  // The theme changed: the toolbars take its style.
  Bind(wxEVT_PAGES_TOOLBAR_STYLE, &MainView::OnToolBarStyle, this);
  // The close button of a pane (the manager sends it to this window).
  Bind(wxEVT_AUI_PANE_CLOSE, &MainView::OnPaneClose, this);
  // What the user double clicks in the file explorer.
  Bind(wxEVT_FILETREE_ACTIVATED, &MainView::OnFileTreeActivated, this);

  EventNotifier::Get()->Bind(wxEVT_APP_PREFS_CHANGED,
                             &MainView::OnAppPrefsSaved, this);
  // A git command runs in the Output View: it shows (OutputView runs it).
  EventNotifier::Get()->Bind(wxEVT_GITVIEW_RUN_IN_TERMINAL,
                             &MainView::OnGitViewRunInTerminal, this);
  // Finalise frame construction.
  FinaliseConstruction();
}

MainView::~MainView() {
  EventNotifier::Get()->Unbind(wxEVT_APP_PREFS_CHANGED,
                               &MainView::OnAppPrefsSaved, this);
  EventNotifier::Get()->Unbind(wxEVT_GITVIEW_RUN_IN_TERMINAL,
                               &MainView::OnGitViewRunInTerminal, this);
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
  BindFocusChildEvent(m_panelSideView,
                      &MainView::OnFileExplorerPanelChildFocus);
  BindFocusChildEvent(m_mainPanel, &MainView::OnMainPanelChildFocus);
  // The terminals come later: wxEVT_CHILD_FOCUS goes up to the pane from them.
  BindFocusChildEvent(m_panelOutputView,
                      &MainView::OnOutputViewPanelChildFocus);
  ApplyPrefs();
}

// ---------------------------------------------------------------------------
// The theme and the file explorer
// ---------------------------------------------------------------------------

void MainView::OnToolBarStyle(wxCommandEvent &event) {
  event.Skip(); // The main toolbar of the frame follows too
  m_sideView->ApplyToolBarStyle();
  m_outputView->ApplyToolBarStyle();
}

void MainView::OnFileTreeActivated(FileEvent &event) {
  EventNotifier::Get()->AddPendingEvent(event);
}

// ---------------------------------------------------------------------------
// The side bar
// ---------------------------------------------------------------------------

void MainView::RestoreLayout() {
  // Deferred: the dock can only get the size it had where the window is big
  // enough for it, and the window is not at its final size (e.g. maximized)
  // before the first pass of the event loop.
  CallAfter([this] {
    const wxString &layout = AppManager::Get().GetPrefs().auiLayout;
    m_sideView->LoadLayout(AppManager::Get().GetPrefs().sideBookLayout);
    // A layout saved by a version with more panes (the activity and the output
    // had their own) does not fit: the side bar starts as it does without one.
    // The check relies on the format of the AUI perspective ("name=<pane>;" for
    // each pane).
    const bool isOld =
        std::any_of(std::begin(kRemovedPanes), std::end(kRemovedPanes),
                    [&layout](const wxString &name) {
                      return layout.Contains("name=" + name + ";");
                    });
    bool restored = false;
    if (!layout.empty() && !isOld) {
      restored = m_auimgr->LoadPerspective(layout, /*update=*/true);
      if (!restored) {
        KLOG_WARN() << "Could not restore the layout of the panes";
      } else if (const wxAuiPaneInfo &pane = m_auimgr->GetPane(kSidePane);
                 !pane.IsDocked() ||
                 (pane.dock_direction != wxAUI_DOCK_LEFT &&
                  pane.dock_direction != wxAUI_DOCK_RIGHT)) {
        // The side bar is on the left or on the right (the menu says which).
        KLOG_WARN() << "The saved layout has the side bar elsewhere: not used";
        m_auimgr->LoadPerspective(m_defaultLayout, /*update=*/true);
        restored = false;
      }
    }
    if (!restored) {
      ApplySideBarDefaultWidth();
    }
    m_layoutRestored = true;
  });
}

void MainView::ResetLayout() {
  if (!m_auimgr->LoadPerspective(m_defaultLayout, /*update=*/true)) {
    KLOG_WARN() << "Could not reset the layout of the panes";
    return;
  }
  ApplySideBarDefaultWidth();
  m_sideView->ResetLayout();
}

bool MainView::IsSideBarShown() const {
  return m_auimgr->GetPane(kSidePane).IsShown();
}

wxDirection MainView::GetSideBarPosition() const {
  return m_auimgr->GetPane(kSidePane).dock_direction == wxAUI_DOCK_LEFT
             ? wxLEFT
             : wxRIGHT;
}

void MainView::SetSideBarPosition(wxDirection position) {
  wxAuiPaneInfo &pane = m_auimgr->GetPane(kSidePane);
  if (!pane.IsOk() || GetSideBarPosition() == position) {
    return;
  }
  // The layout is saved with the others (SaveLayout()).
  pane.Dock().Direction(position == wxLEFT ? wxAUI_DOCK_LEFT
                                           : wxAUI_DOCK_RIGHT);
  m_auimgr->Update();
}

void MainView::ShowSideBar(bool show) {
  wxWindowUpdateLocker locker{m_mainPanel};
  wxAuiPaneInfo &pane = m_auimgr->GetPane(kSidePane);
  if (!pane.IsOk() || pane.IsShown() == show) {
    return;
  }
  pane.Show(show);
  m_auimgr->Update();
}

bool MainView::IsOutputViewShown() const {
  return m_auimgr->GetPane(kOutputViewPane).IsShown();
}

void MainView::ShowOutputView(bool show) {
  wxAuiPaneInfo &pane = m_auimgr->GetPane(kOutputViewPane);
  if (!pane.IsOk() || pane.IsShown() == show) {
    return;
  }
  if (!show) {
    // The keys must not go to a hidden terminal: the page that shows gets
    // them.
    wxWindow *focus = wxWindow::FindFocus();
    const bool hadFocus =
        focus != nullptr &&
        (focus == m_panelOutputView || m_panelOutputView->IsDescendant(focus));
    KeepOutputViewSize(pane);
    pane.Hide();
    m_auimgr->Update();
    if (hadFocus) {
      if (wxWindow *page = m_sessionsBook->GetCurrentPage()) {
        page->SetFocus();
      }
    }
    return;
  }
  pane.Show();
  if (!pane.IsDocked() || pane.best_size.y <= 0) {
    m_auimgr->Update();
    return;
  }
  // A hidden pane has no dock: a new one is made, as high as BestSize() but at
  // most 30% of the window. A minimum size makes the dock that high; it is
  // dropped again so that the user can make the pane smaller.
  const wxSize minSize = pane.min_size;
  pane.MinSize(-1, pane.best_size.y);
  m_auimgr->Update();
  m_auimgr->GetPane(kOutputViewPane).MinSize(minSize);
  m_auimgr->Update();
}

void MainView::KeepOutputViewSize(wxAuiPaneInfo &pane) {
  // Not floating: a floating pane keeps its size itself.
  if (pane.IsShown() && pane.IsDocked() && pane.window != nullptr) {
    pane.BestSize(pane.window->GetSize());
  }
}

void MainView::OnPaneClose(wxAuiManagerEvent &event) {
  event.Skip(); // The manager hides it
  if (wxAuiPaneInfo *pane = event.GetPane();
      pane != nullptr && pane->name == kOutputViewPane) {
    KeepOutputViewSize(*pane);
  }
}

void MainView::OnGitViewRunInTerminal(FileEvent &event) {
  event.Skip(); // OutputView runs it
  ShowOutputView(true);
}

void MainView::ShowSideTab(SideView::Tab tab) {
  ShowSideBar(true);
  m_sideView->SelectTab(tab);
}

FindResultsView *MainView::GetFindInFiles() const {
  return m_outputView->GetFindInFiles();
}

void MainView::ShowOutputTab(OutputView::Tab tab) {
  ShowOutputView(true);
  m_outputView->SelectTab(tab);
}

void MainView::ApplySideBarDefaultWidth() {
  // A dock keeps the size it got when it was created (and a new dock is at most
  // 30% of the window wide), so BestSize() cannot change it any more. A minimum
  // size makes the dock at least that wide; it is dropped again so the user can
  // make the pane narrower.
  const int width = GetClientSize().x * kSidePaneWidthPercent / 100;
  m_auimgr->GetPane(kSidePane).BestSize(width, -1).MinSize(width, -1);
  m_auimgr->Update();
  m_auimgr->GetPane(kSidePane).MinSize(wxDefaultSize);
  m_auimgr->Update();
}

void MainView::SaveLayout() {
  // Before the saved layout was applied the panes still hold their defaults,
  // which must not replace what was saved (see RestoreLayout()).
  if (m_layoutRestored) {
    AppManager::Get().GetPrefs().auiLayout = m_auimgr->SavePerspective();
    AppManager::Get().GetPrefs().sideBookLayout = m_sideView->SaveLayout();
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

  for (const char *name :
       {"line-numbers", "highlight-word", "bookmark-toggle", "bookmark-next",
        "bookmark-previous", "bookmark-clear"}) {
    bmps.Load(wxString(name) + ".svg");
    bmps.AddAlias(wxString(name) + ".svg", name);
  }

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
  if (MakeActive(m_mainPanel)) {
    ScheduleAuiUpdate();
  }
}

void MainView::OnFileExplorerPanelChildFocus(wxChildFocusEvent &event) {
  event.Skip();
  if (MakeActive(m_panelSideView)) {
    ScheduleAuiUpdate();
  }
}

void MainView::OnOutputViewPanelChildFocus(wxChildFocusEvent &event) {
  event.Skip();
  if (MakeActive(m_panelOutputView)) {
    ScheduleAuiUpdate();
  }
}

void MainView::ScheduleAuiUpdate() {
  if (m_auiUpdatePending) {
    return;
  }
  m_auiUpdatePending = true;
  CallAfter([this] {
    m_auiUpdatePending = false;
    m_auimgr->Update();
  });
}

void MainView::OnAppPrefsSaved(wxCommandEvent &event) {
  event.Skip();
  wxWindowUpdateLocker locker{this};
  ApplyPrefs();
}

void MainView::ApplyPrefs() {
  m_sideView->ApplyPrefs();
  m_outputView->ApplyPrefs();
}
