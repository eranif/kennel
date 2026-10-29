#include "MainView.hpp"

#include "ThemeLoader.h"
#include "ThemeManager.h"
#include "app/ActivityView.hpp"
#include "app/AssetBootstrap.h"
#include "app/FileExplorerView.hpp"
#include "app/PageNames.h"
#include "core/AppManager.h"
#include "core/EventNotifier.hpp"
#include "core/Logger.h"

#include <algorithm>
#include <vector>
#include <wx/dir.h>
#include <wx/sizer.h>

namespace {
// The width the side panes start with when there is no saved layout, in DIP.
constexpr int kSidePaneWidthDip = 300;
// The size of a side pane that is dragged out of the window, in DIP. A saved
// layout has the size the user gave it.
constexpr int kFloatingPaneWidthDip = 400;
constexpr int kFloatingPaneHeightDip = 600;
// The names of the side panes (see MainViewBase).
const wxString kSidePanes[] = {"FileBrowser", "Activity"};

// What the choice of the file explorer shows for a session: the name, and the
// group when the session is in one.
wxString ExplorerLabel(const SessionRef &ref) {
  return ref.groupName == kTerminalsGroupName
             ? ref.name
             : ref.name + " (" + ref.groupName + ")";
}
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

  m_fileExplorer = new FileExplorerView(GetPanelFileExplorer());
  GetPanelFileExplorer()->GetSizer()->Add(m_fileExplorer,
                                          wxSizerFlags(1).Expand());
  m_activityView = new ActivityView(GetPanelActivity());
  GetPanelActivity()->GetSizer()->Add(m_activityView, wxSizerFlags(1).Expand());
  // The size a pane gets when it floats (it only matters then, so no Update()).
  for (const wxString &name : kSidePanes) {
    m_auimgr->GetPane(name).FloatingSize(
        FromDIP(wxSize(kFloatingPaneWidthDip, kFloatingPaneHeightDip)));
  }

  m_defaultLayout = m_auimgr->SavePerspective();

  // What the pages tell: the side views follow.
  Bind(wxEVT_SESSION_PAGE_ADDED, &MainView::OnSessionPageAdded, this);
  Bind(wxEVT_SESSION_PAGE_REMOVED, &MainView::OnSessionPageRemoved, this);
  Bind(wxEVT_SESSION_PAGE_RENAMED, &MainView::OnSessionPageRenamed, this);
  Bind(wxEVT_SESSION_PAGES_CLEARED, &MainView::OnSessionPagesCleared, this);
  Bind(wxEVT_SESSION_PAGE_SHOWN, &MainView::OnSessionPageShown, this);
  Bind(wxEVT_SESSION_PAGE_ACTIVITY, &MainView::OnSessionPageActivity, this);
  Bind(wxEVT_FILE_REVEAL, &MainView::OnFileReveal, this);
  Bind(wxEVT_PAGES_TOOLBAR_STYLE, &MainView::OnToolBarStyle, this);
  // What the user asks in the file explorer.
  Bind(wxEVT_FILETREE_ACTIVATED, &MainView::OnFileTreeActivated, this);
  Bind(wxEVT_FILEEXPLORER_SESSION_SELECTED, &MainView::OnExplorerSession, this);
  // What the user asks in the activity view.
  Bind(wxEVT_ACTIVITYVIEW_SESSION_SELECTED, &MainView::OnActivitySession, this);
}

// ---------------------------------------------------------------------------
// From the pages
// ---------------------------------------------------------------------------

void MainView::OnSessionPageAdded(PageViewEvent &event) {
  const SessionRef &ref = event.GetRef();
  m_fileExplorer->AddSession(ref, ExplorerLabel(ref), event.GetAccount());
}

void MainView::OnSessionPageRemoved(PageViewEvent &event) {
  m_fileExplorer->RemoveSession(event.GetRef());
  m_activityView->RemoveSession(event.GetRef());
}

void MainView::OnSessionPageRenamed(PageViewEvent &event) {
  m_fileExplorer->RenameSession(event.GetOldRef(), event.GetRef(),
                                ExplorerLabel(event.GetRef()));
  m_activityView->RenameSession(event.GetOldRef(), event.GetRef());
}

void MainView::OnSessionPagesCleared(wxCommandEvent &) {
  m_fileExplorer->Clear();
  m_activityView->Clear();
}

void MainView::OnSessionPageShown(PageViewEvent &event) {
  // Selects what is to be shown; nothing is sent back.
  m_fileExplorer->SetActiveSession(event.GetRef());
  m_activityView->SetActiveSession(event.GetRef());
}

void MainView::OnSessionPageActivity(PageViewEvent &event) {
  m_activityView->SetActivity(event.GetRef(), event.GetActivity());
}

void MainView::OnFileReveal(FileEvent &event) {
  // Selects the file; nothing is sent back.
  m_fileExplorer->RevealFile(event.GetAccount(), event.GetPath());
  // A file is showing, not a session.
  m_activityView->SetActiveSession(std::nullopt);
}

void MainView::OnToolBarStyle(wxCommandEvent &) {
  m_fileExplorer->ApplyToolBarStyle();
}

// ---------------------------------------------------------------------------
// From the file explorer
// ---------------------------------------------------------------------------

void MainView::OnExplorerSession(PageViewEvent &event) {
  EventNotifier::Get()->AddPendingEvent(event);
}

void MainView::OnFileTreeActivated(FileEvent &event) {
  EventNotifier::Get()->AddPendingEvent(event);
}

// ---------------------------------------------------------------------------
// From the activity view
// ---------------------------------------------------------------------------

void MainView::OnActivitySession(PageViewEvent &event) {
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
    bool restored = false;
    if (!layout.empty()) {
      restored = m_auimgr->LoadPerspective(layout, /*update=*/true);
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
      wxAuiPaneInfoArray &panes = m_auimgr->GetAllPanes();
      for (size_t i = 0; i < panes.GetCount(); ++i) {
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

  bmps.Load("link.svg");
  bmps.AddAlias("link.svg", "link");

  // The status of the activity view.
  bmps.Load("hand.svg");
  bmps.AddAlias("hand.svg", "hand");

  bmps.Load("check.svg");
  bmps.AddAlias("check.svg", "check");

  for (int i = 0; i < 8; ++i) {
    const wxString file = wxString::Format("spinner-%d.svg", i);
    bmps.Load(file);
    bmps.AddAlias(file, wxString::Format("spinner-%d", i));
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

  bmps.Load("pin.svg");
  bmps.AddAlias("pin.svg", "pin");

  bmps.Load("save.svg");
  bmps.AddAlias("save.svg", "save");

  bmps.Load("reload.svg");
  bmps.AddAlias("reload.svg", "reload");

  bmps.Load("close.svg");
  bmps.AddAlias("close.svg", "close");

  bmps.Load("copy.svg");
  bmps.AddAlias("copy.svg", "copy");

  bmps.Load("paste.svg");
  bmps.AddAlias("paste.svg", "paste");

  bmps.Load("find.svg");
  bmps.AddAlias("find.svg", "find");

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
