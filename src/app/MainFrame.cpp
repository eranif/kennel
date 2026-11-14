#include "app/MainFrame.h"
#include "app/AboutDialog.hpp"
#include "app/AgentScan.hpp"
#include "app/AppTheme.h"
#include "app/AssetBootstrap.h"
#include "app/EditAgentsDlg.hpp"
#include "app/EditHosts.hpp"
#include "app/EditJobsDlg.hpp"
#include "app/EditMenu.hpp"
#include "app/FileActions.h"
#include "app/FilePage.hpp"
#include "app/FindInFilesDlg.hpp"
#include "app/FindResultsView.hpp"
#include "app/GotoAnythingDlg.hpp"
#include "app/JobLogViewer.hpp"
#include "app/NewAgentWizard.hpp"
#include "app/OutputView.hpp"
#include "app/PageNames.h"
#include "app/PageViewEvent.hpp"
#include "app/PopupStyle.hpp"
#include "app/ReviewBuddyMenu.hpp"
#include "app/SessionPage.hpp"
#include "app/SettingsDlg.hpp"
#include "app/ToolBarArt.h"
#include "app/ToolBarUtils.h"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/EventNotifier.hpp"
#include "core/Logger.h"
#include "core/RemoteInstaller.h"
#include "core/UpdateChecker.h"
#include "core/Version.h"
#include "core/WslPath.h"
#include <wx/sysopt.h>

#include "terminal_view.h"
#include <algorithm>
#include <optional>
#include <wx/app.h>
#include <wx/artprov.h>
#include <wx/bmpbndl.h>
#include <wx/choicdlg.h>
#include <wx/display.h>
#include <wx/fontdlg.h>
#include <wx/iconbndl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/toolbar.h>

namespace {
// Client tools get sequential wx ids starting here.
constexpr int kFirstClientToolId = wxID_HIGHEST + 1;
// View -> Theme items get sequential ids from here (kept well clear of the
// per-adapter client tool ids above).
constexpr int kFirstThemeMenuId = wxID_HIGHEST + 1000;
// View -> Change terminal font.
constexpr int kChangeFontMenuId = wxID_HIGHEST + 2000;
} // namespace

static MainFrame *mainFrame{nullptr};

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, kAppName, wxDefaultPosition,
              wxSize(1280, 800)) {
  CreateStatusFields();

  KLOG_DEBUG() << "Creating main frame";
  // Window/taskbar icon: render the app SVG at a few common sizes so the
  // platform can pick the crispest for each context (title bar, alt-tab, etc.).
  const wxString appIcon = ResolveIconPath("kennel.svg");
  if (!appIcon.empty() && wxFileName::FileExists(appIcon)) {
    wxIconBundle icons;
    for (int px : {16, 32, 48, 64, 128, 256}) {
      const wxBitmapBundle bb =
          wxBitmapBundle::FromSVGFile(appIcon, wxSize(px, px));
      if (bb.IsOk()) {
        icons.AddIcon(bb.GetIcon(wxSize(px, px)));
      }
    }
    if (!icons.IsEmpty()) {
      SetIcons(icons);
    }
  } else {
    KLOG_WARN() << "App icon 'kennel.svg' not found; window uses default icon";
  }

  // The main view loads the available themes, which the View menu needs, so
  // create it before the menu bar. BuildToolBar() populates m_clientToolIds,
  // which BuildMenuBar()'s Launch menu reuses.
  SetSizer(new wxBoxSizer(wxHORIZONTAL));
  m_mainView = new MainView(this);
  GetSizer()->Add(m_mainView, wxSizerFlags(1).Expand());

  BuildToolBar();
  BuildMenuBar();

  // Restore the persisted window geometry. A negative x or y (-1 is "nothing
  // saved", and a window off the main screen has a negative one) is not used:
  // the window gets the default size and place, most of the screen. Otherwise
  // the saved size and position apply. If the window was maximized last time,
  // maximize after setting the restore bounds.
  const WindowGeometry &g = AppManager::Get().GetPrefs().window;
  const wxPoint pos =
      (g.x >= 0 && g.y >= 0) ? wxPoint(g.x, g.y) : wxDefaultPosition;
  if (pos == wxDefaultPosition) {
    // Nothing saved yet (a first run): most of the screen, not a small window.
    // The size is in pixels, so a high resolution screen gets a big window too.
    const wxRect area = wxDisplay(this).GetClientArea();
    wxSize size(std::max(FromDIP(1280), area.width * 8 / 10),
                std::max(FromDIP(800), area.height * 8 / 10));
    size.DecTo(area.GetSize()); // Not bigger than the screen
    SetSize(wxRect(area.GetPosition() + (area.GetSize() - size) / 2, size));
  } else if (g.width > 0 && g.height > 0) {
    SetSize(wxRect(pos, wxSize(g.width, g.height)));
  } else if (pos != wxDefaultPosition) {
    Move(pos);
  }
  if (g.maximized) {
    Maximize(true);
  }
  m_mainView->RestoreLayout();

  Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
  Bind(wxEVT_ACTIVATE, &MainFrame::OnActivate, this);
  Bind(wxEVT_SESSION_STATUS, &MainFrame::OnSessionStatus, this);
  mainFrame = this;

  // One shot, 500ms after the frame is constructed.
  m_startupTimer.SetOwner(this, kStartupTimerId);
  Bind(wxEVT_TIMER, &MainFrame::OnStartupTimer, this, kStartupTimerId);
  m_startupTimer.StartOnce(500);
}

void MainFrame::OnStartupTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  FinaliseStartup();
}

void MainFrame::FinaliseStartup() {
  // Called once, shortly after the start of the application.
  // Rebuild any sessions persisted in workspace.json (resuming where
  // possible).
  m_mainView->GetSessionsBook()->RestoreSessions();
  AppManager::Get().SavePrefs();

  // Jobs run in a fresh terminal tab each time they fire, so the scheduler
  // just needs a callback into the view; it owns no UI state of its own.
  m_jobScheduler = std::make_unique<JobScheduler>([this](const JobDef &job) {
    m_mainView->GetSessionsBook()->RunJob(job);
  });
  m_jobScheduler->Reload();

  if (AppManager::Get().GetPrefs().checkForUpdatesOnStartup) {
    CheckForUpdates(/*silent=*/true);
  }

  EventNotifier::Get()->Bind(wxEVT_AGENT_SCAN, &MainFrame::OnAgentScan, this);
  StartLocalAgentScan();
}

void MainFrame::CreateStatusFields() {
  CreateStatusBar(kFieldCount);
  // The main text and the title share what is left, the title gets more.
  const int widths[kFieldCount] = {-1,           -2,           FromDIP(30),
                                   FromDIP(300), FromDIP(160), FromDIP(26),
                                   FromDIP(300)};
  SetStatusWidths(kFieldCount, widths);
  // No border around the fields.
  int styles[kFieldCount];
  std::fill(std::begin(styles), std::end(styles), wxSB_FLAT);
  GetStatusBar()->SetStatusStyles(kFieldCount, styles);
  m_baseText = wxString::Format("%s %s", kAppName, kAppVersion);
  SetStatusText(m_baseText, kFieldText);

  wxStatusBar *bar = GetStatusBar();
  m_sessionIcon = new wxStaticBitmap(bar, wxID_ANY, wxBitmapBundle());
  m_sessionIcon->Hide();
  m_statusIndicator = new wxActivityIndicator(bar);
  m_statusIndicator->Hide();
  bar->Bind(wxEVT_SIZE, &MainFrame::OnStatusBarSize, this);
  LayoutStatusChildren();
}

void MainFrame::OnStatusBarSize(wxSizeEvent &event) {
  event.Skip();
  LayoutStatusChildren();
}

void MainFrame::LayoutStatusChildren() {
  wxStatusBar *bar = GetStatusBar();
  CHECK_NOT_NULL_RETURN(bar);
  wxRect rect;
  if (m_statusIndicator != nullptr &&
      bar->GetFieldRect(kFieldIndicator, rect)) {
    // A square, in the middle of the field.
    const int side =
        std::max(1, std::min(rect.GetWidth(), rect.GetHeight()) - 4);
    m_statusIndicator->SetSize(wxRect(wxSize(side, side)).CenterIn(rect));
  }
  if (m_sessionIcon != nullptr && bar->GetFieldRect(kFieldIcon, rect)) {
    m_sessionIcon->Move(
        wxRect(m_sessionIcon->GetSize()).CenterIn(rect).GetTopLeft());
  }
}

void MainFrame::UpdateIndicator() {
  CHECK_NOT_NULL_RETURN(m_statusIndicator);
  if (m_activityBusy || m_sessionBusy) {
    m_statusIndicator->Show();
    m_statusIndicator->Start();
  } else {
    m_statusIndicator->Stop();
    m_statusIndicator->Hide();
  }
}

void MainFrame::OnSessionStatus(SessionStatusEvent &event) {
  CHECK_NOT_NULL_RETURN(m_sessionIcon);
  SetStatusText(event.GetString(), kFieldTitle);
  SetStatusText(event.GetReviewText(), kFieldReview);
  SetStatusText(event.GetHost(), kFieldHost);
  SetStatusText(event.GetSessionLabel(), kFieldSession);
  // The fields cut long texts: all of it is in the tooltip of the bar.
  GetStatusBar()->SetToolTip(event.GetTooltip());
  if (event.GetIcon().IsOk()) {
    m_sessionIcon->SetBitmap(event.GetIcon());
    m_sessionIcon->SetSize(m_sessionIcon->GetBestSize());
    LayoutStatusChildren();
    m_sessionIcon->Show();
  } else {
    m_sessionIcon->Hide();
  }
  m_sessionBusy = event.IsBusy();
  UpdateIndicator();
}

void MainFrame::ClearSessionStatus() {
  CHECK_NOT_NULL_RETURN(m_sessionIcon);
  SetStatusText(wxEmptyString, kFieldTitle);
  GetStatusBar()->SetToolTip(wxEmptyString);
  SetStatusText(wxEmptyString, kFieldReview);
  SetStatusText(wxEmptyString, kFieldHost);
  SetStatusText(wxEmptyString, kFieldSession);
  m_sessionIcon->Hide();
  m_sessionBusy = false;
  UpdateIndicator();
}

MainFrame::~MainFrame() {
  // A scan that is still running ends without telling us, and so does an
  // install of a tool.
  AgentScan::Stop();
  RemoteInstaller::Stop();
  EventNotifier::Get()->Unbind(wxEVT_AGENT_SCAN, &MainFrame::OnAgentScan, this);
  mainFrame = nullptr;
  Unbind(wxEVT_ACTIVATE, &MainFrame::OnActivate, this);
}

void MainFrame::OnClose(wxCloseEvent &evt) {
  // Tell whoever needs the user first: a file with changes asks Save / Discard
  // / Cancel, and Cancel keeps the application open.
  wxCloseEvent closing(wxEVT_APP_CLOSING);
  closing.SetCanVeto(evt.CanVeto());
  EventNotifier::Get()->ProcessEvent(closing);
  if (closing.GetVeto()) {
    evt.Veto();
    return;
  }

  // Capture the restore bounds (un-maximized geometry) so a maximized session
  // does not persist a full-screen rect as the normal size.
  auto &prefs = AppManager::Get().GetPrefs();
  prefs.window.maximized = IsMaximized();
  if (!IsMaximized()) {
    const wxRect r = GetRect();
    prefs.window.x = r.x;
    prefs.window.y = r.y;
    prefs.window.width = r.width;
    prefs.window.height = r.height;
  }
  // A minimized window reports a collapsed layout, which is not the user's.
  if (!IsIconized()) {
    m_mainView->SaveLayout();
  }
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not persist window geometry: " << st.message();
  }

  // No save-on-exit dance: sessions resume via the client's own
  // --resume/--continue, so there is nothing to flush before quitting.
  evt.Skip(); // continue with the default close (destroys the frame)
}

void MainFrame::BuildToolBar() {
  // A wxAuiToolBar on the left of the main view, on every platform (the native
  // toolbar of a frame is always horizontal, and can not take an art). It takes
  // the icons at the size of their bundles. KennelToolBarArt draws it and
  // follows the theme of the application.
  m_toolBar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxAUI_TB_VERTICAL);
  // The other styles of the art are not used for now.
  m_toolBar->SetArtProvider(
      new KennelToolBarArt(KennelToolBarArt::Style::Accent, /*large=*/true));
  GetSizer()->Insert(0, m_toolBar, wxSizerFlags(0).Expand());
  Bind(wxEVT_PAGES_TOOLBAR_STYLE, &MainFrame::OnToolBarStyle, this);

  auto &bmps = AppManager::Get().GetBitmaps();

  Bind(wxEVT_TOOL, &MainFrame::OnRefreshSession, this, wxID_REFRESH);
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnRefreshSessionUI, this, wxID_REFRESH);
  Bind(wxEVT_TOOL, &MainFrame::OnStartAgent, this, XRCID("start-agent"));
  Bind(wxEVT_TOOL, &MainFrame::OnNewTerminal, this, XRCID("start-terminal"));
  Bind(wxEVT_TOOL, &MainFrame::OnManageJobs, this, XRCID("manage-jobs"));
  Bind(wxEVT_TOOL, &MainFrame::OnArchivedSessions, this,
       XRCID("archived-sessions"));

  // Static content first...
  toolbar::AddTool(m_toolBar, XRCID("start-agent"), _("Start a New Agent"),
                   bmps.Get("agent", true), _("Start a New Agent"));
  toolbar::AddTool(m_toolBar, XRCID("start-terminal"),
                   _("Start a New Terminal"), bmps.Get("terminal", true),
                   _("Start a New Terminal"));
  // The jobs and the archive are one group.
  m_toolBar->AddSeparator();
  toolbar::AddTool(m_toolBar, XRCID("manage-jobs"), _("Manage Jobs"),
                   bmps.Get("job", true), _("Manage Jobs"));
  toolbar::AddTool(m_toolBar, XRCID("archived-sessions"), _("Archive"),
                   bmps.Get("archive", true),
                   _("Restore or delete the archived sessions"));
  m_toolBar->AddSeparator();
  BuildLaunchTools();
  m_toolBar->Realize();
  toolbar::ApplyStyle(m_toolBar);
}

void MainFrame::OnToolBarStyle(wxCommandEvent &event) {
  event.Skip();
  toolbar::ApplyStyle(m_toolBar);
}

void MainFrame::BuildLaunchTools() {
  for (const auto &[toolId, clientName] : m_clientToolIdToName) {
    Unbind(wxEVT_TOOL, &MainFrame::OnStartAgentFromToolBar, this, toolId);
    m_toolBar->DeleteTool(toolId);
  }
  m_clientToolIdToName.clear();

  auto &bmps = AppManager::Get().GetBitmaps();

  const AdapterRegistry &registry = AppManager::Get().Adapters();
  auto agents = registry.AgentNames();
  std::sort(agents.begin(), agents.end());
  for (const wxString &agentName : agents) {
    const AgentDef *agent = registry.FindAgent(agentName);
    if (agent == nullptr) {
      continue;
    }

    bmps.Load(agent->iconPath);
    bmps.AddAlias(agent->iconPath, agent->name);

    const wxString iconName = agent->iconPath;
    bmps.Load(iconName);
    wxBitmapBundle icon = bmps.Get(iconName, true);

    int toolId = wxXmlResource::GetXRCID(agent->name);
    toolbar::AddTool(m_toolBar, toolId, agent->name, icon,
                     wxString::Format("Launch %s", agent->name));
    m_clientToolIdToName.insert({toolId, agent->name});
    Bind(wxEVT_TOOL, &MainFrame::OnStartAgentFromToolBar, this, toolId);
  }
}

void MainFrame::BuildMenuBar() {
  auto *menuBar = new wxMenuBar();

  auto *fileMenu = new wxMenu();
#ifndef __WXMAC__
  fileMenu->Append(wxID_ABOUT, _("&About Kennel"));
  Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
  fileMenu->AppendSeparator();
#endif
  fileMenu->Append(
      XRCID("new-file"), _("New &File\tCtrl+N"),
      _("Open an empty editor: it asks for the file when you save"));
  Bind(wxEVT_MENU, &MainFrame::OnNewFile, this, XRCID("new-file"));
  fileMenu->Append(XRCID("open-file"), _("&Open File...\tCtrl+Shift+O"),
                   _("Open a file in an editor"));
  Bind(wxEVT_MENU, &MainFrame::OnOpenFile, this, XRCID("open-file"));
  fileMenu->AppendSeparator();
  fileMenu->Append(XRCID("start-agent"), _("&Start Agent...\tCtrl+T"),
                   _("Start a new agent session"));
  Bind(wxEVT_MENU, &MainFrame::OnStartAgent, this, XRCID("start-agent"));
  fileMenu->Append(XRCID("start-terminal"), _("New Terminal\tCtrl+E"),
                   _("Start GlyphT Terminal"));
  Bind(wxEVT_MENU, &MainFrame::OnNewTerminal, this, XRCID("start-terminal"));
  fileMenu->Append(wxID_REFRESH, _("&Refresh Current Selection\tF5"),
                   _("Refresh the Current Selection"));
  Bind(wxEVT_MENU, &MainFrame::OnRefreshSession, this, wxID_REFRESH);
  fileMenu->Append(XRCID("rename-selection"), _("Rename Session...\tF2"),
                   _("Rename the selected session"));
  Bind(wxEVT_MENU, &MainFrame::OnRenameItem, this, XRCID("rename-selection"));
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnRenameItemUI, this,
       XRCID("rename-selection"));
  fileMenu->AppendSeparator();
  fileMenu->Append(wxID_NEW, _("Configure &New Agent..."),
                   _("Define a New Agent"));
  Bind(wxEVT_MENU, &MainFrame::OnNewAgent, this, wxID_NEW);
  fileMenu->AppendSeparator();
  fileMenu->Append(wxID_CLOSE_ALL, _("Close All Sessions"),
                   _("Close every session in every group"));
  Bind(wxEVT_MENU, &MainFrame::OnCloseAllSessions, this, wxID_CLOSE_ALL);
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnCloseAllSessionsUI, this, wxID_CLOSE_ALL);
  // AcceleratorInterceptor handles the key (a terminal would get it
  // otherwise); the text after the tab shows it.
  fileMenu->Append(XRCID("archived-sessions"),
                   _("Archived Sessions...\tCtrl+Shift+A"),
                   _("Restore or delete the archived sessions"));
  Bind(wxEVT_MENU, &MainFrame::OnArchivedSessions, this,
       XRCID("archived-sessions"));

#ifndef __WXMAC__
  fileMenu->AppendSeparator();
  fileMenu->Append(wxID_EXIT);
#endif
  menuBar->Append(fileMenu, "&File");

  BuildEditMenu(menuBar);
  BuildSearchMenu(menuBar);
  BuildViewMenu(menuBar);
  BuildJobsMenu(menuBar);
  BuildReviewBuddyMenu(menuBar);
  BuildSettingsMenu(menuBar);
  SetMenuBar(menuBar);
  ShowShortcutsInToolTips();

  Bind(wxEVT_MENU, [this](wxCommandEvent &) { Close(); }, wxID_EXIT);
}

void MainFrame::ShowShortcutsInToolTips() {
  // A tool of the toolbar and an item of the menus with the same id are the
  // same command: the tool shows the shortcut of the item, so there is one
  // place that says what the key is.
  wxMenuBar *menuBar = GetMenuBar();
  for (size_t i = 0; i < m_toolBar->GetToolCount(); ++i) {
    wxAuiToolBarItem *tool = m_toolBar->FindToolByIndex(i);
    if (tool == nullptr || tool->GetKind() == wxITEM_SEPARATOR) {
      continue;
    }
    wxMenuItem *item = menuBar->FindItem(tool->GetId());
    if (item == nullptr) {
      continue;
    }
    std::unique_ptr<wxAcceleratorEntry> accel(item->GetAccel());
    if (accel == nullptr) {
      continue;
    }
    m_toolBar->SetToolShortHelp(
        tool->GetId(),
        wxString::Format("%s\n%s", tool->GetShortHelp(), accel->ToString()));
  }
}

void MainFrame::BuildSearchMenu(wxMenuBar *menuBar) {
  auto *searchMenu = new wxMenu();
#ifdef __WXOSX__
  // macOS uses Ctrl+Tab to move the keyboard focus before the focused view
  // gets the key, so it never reaches AcceleratorInterceptor. A menu key
  // equivalent is checked before that. "RawCtrl" is the physical Ctrl key,
  // not Cmd.
  searchMenu->Append(wxID_FORWARD, _("Select Next Page\tRawCtrl+Tab"),
                     _("Select the next session or file"));
  searchMenu->Append(wxID_BACKWARD,
                     _("Select Previous Page\tRawCtrl+Shift+Tab"),
                     _("Select the previous session or file"));
#else
  // AcceleratorInterceptor handles Ctrl+Tab / Ctrl+Shift+Tab before the menu
  // sees them; the text after the tab shows them.
  searchMenu->Append(wxID_FORWARD, _("Select Next Page\tCtrl+Tab"),
                     _("Select the next session or file"));
  searchMenu->Append(wxID_BACKWARD, _("Select Previous Page\tCtrl+Shift+Tab"),
                     _("Select the previous session or file"));
#endif

  // Session movement: cycles through every open session, regardless of
  // which group it belongs to.
  Bind(wxEVT_MENU, &MainFrame::OnNextSession, this, wxID_FORWARD);
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnPrevSessionUI, this, wxID_BACKWARD);
  Bind(wxEVT_MENU, &MainFrame::OnPrevSession, this, wxID_BACKWARD);
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnNextSessionUI, this, wxID_FORWARD);
  searchMenu->AppendSeparator();
  // AcceleratorInterceptor handles the key before the menu sees it (a terminal
  // would get it otherwise); the text after the tab shows it.
  searchMenu->Append(XRCID("goto-anything"),
                     _("Goto Anything...\tCtrl+Shift+P"),
                     _("Search the menus, the pages and the actions of the "
                       "page that is showing"));
  Bind(wxEVT_MENU, &MainFrame::OnGotoAnything, this, XRCID("goto-anything"));
  searchMenu->AppendSeparator();
  // AcceleratorInterceptor handles these keys before the menu sees them (the
  // terminal would get them otherwise); the text after the tab shows them.
  searchMenu->Append(wxID_FIND, _("Find...\tCtrl+F"),
                     _("Find text in the file or the terminal"));
  searchMenu->Append(XRCID("find-next"), _("Find Next\tF3"));
  searchMenu->Append(XRCID("find-previous"), _("Find Previous\tShift+F3"));
  Bind(wxEVT_MENU, &MainFrame::OnFind, this, wxID_FIND);
  Bind(wxEVT_MENU, &MainFrame::OnFindNext, this, XRCID("find-next"));
  Bind(wxEVT_MENU, &MainFrame::OnFindNext, this, XRCID("find-previous"));
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnFindUI, this, wxID_FIND);
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnFindUI, this, XRCID("find-next"));
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnFindUI, this, XRCID("find-previous"));
  // AcceleratorInterceptor handles the key (a terminal would get it otherwise).
  searchMenu->Append(XRCID("find-in-files"),
                     _("Find in Files...\tCtrl+Shift+F"),
                     _("Search the files of a folder, on this machine, in WSL "
                       "or on a remote host"));
  Bind(wxEVT_MENU, &MainFrame::OnFindInFiles, this, XRCID("find-in-files"));
#ifdef __WXOSX__
  searchMenu->Append(XRCID("goto-line"), _("Go to Line...\tRawCtrl+G"));
#else
  // AcceleratorInterceptor handles the key (a terminal keeps Ctrl+G); the text
  // after the tab shows it.
  searchMenu->Append(XRCID("goto-line"), _("Go to Line...\tCtrl+G"));
#endif
  Bind(wxEVT_MENU, &MainFrame::OnGotoLine, this, XRCID("goto-line"));
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnGotoLineUI, this, XRCID("goto-line"));
  menuBar->Append(searchMenu, _("Search"));
}

void MainFrame::BuildViewMenu(wxMenuBar *menuBar) {
  auto *viewMenu = new wxMenu();
  viewMenu->AppendCheckItem(XRCID("show-side-view"), _("Side Bar"),
                            _("Show or hide the side bar"));
  viewMenu->AppendCheckItem(XRCID("show-activity"), _("Activity"),
                            _("Show or hide the activity view"));
#ifdef __WXOSX__
  // The physical Ctrl key, not Cmd (Cmd+` switches the windows of the system).
  const wxString outputLabel = _("Output	RawCtrl+`");
#else
  // AcceleratorInterceptor handles the key (a terminal would get it otherwise).
  const wxString outputLabel = _("Output	Ctrl+`");
#endif
  viewMenu->AppendCheckItem(XRCID("show-output"), outputLabel,
                            _("Show or hide the output pane"));
#ifdef __WXOSX__
  // The physical Ctrl key, not Cmd.
  const wxString toggleLabel = _("Toggle All Views\tRawCtrl+M");
#else
  // AcceleratorInterceptor handles the key (a terminal would get it otherwise);
  // the text after the tab shows it.
  const wxString toggleLabel = _("Toggle All Views\tCtrl+M");
#endif
  viewMenu->Append(XRCID("toggle-all-views"), toggleLabel,
                   _("Hide all the views except for the main view, or show "
                     "them again"));
  viewMenu->AppendSeparator();
  Bind(wxEVT_MENU, &MainFrame::OnShowPane, this, XRCID("show-side-view"));
  Bind(wxEVT_MENU, &MainFrame::OnShowPane, this, XRCID("show-activity"));
  Bind(wxEVT_MENU, &MainFrame::OnShowPane, this, XRCID("show-output"));
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnShowPaneUI, this,
       XRCID("show-side-view"));
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnShowPaneUI, this, XRCID("show-activity"));
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnShowPaneUI, this, XRCID("show-output"));
  Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { m_mainView->ToggleAllPanes(); },
      XRCID("toggle-all-views"));
  // What the close button of the tab of an agent session does (the choice of
  // the "Do not ask again" box of the question it asks).
  auto *closeMenu = new wxMenu();
  closeMenu->AppendRadioItem(XRCID("close-action-ask"), _("Ask"),
                             _("Ask whether to archive or close"));
  closeMenu->AppendRadioItem(XRCID("close-action-archive"), _("Archive"),
                             _("Archive the session, so it can be restored"));
  closeMenu->AppendRadioItem(XRCID("close-action-close"), _("Close"),
                             _("Close the session"));
  for (const char *name :
       {"close-action-ask", "close-action-archive", "close-action-close"}) {
    Bind(wxEVT_MENU, &MainFrame::OnCloseAction, this, XRCID(name));
    Bind(wxEVT_UPDATE_UI, &MainFrame::OnCloseActionUI, this, XRCID(name));
  }
  viewMenu->AppendSubMenu(closeMenu, _("Close Button of a Session"));
  viewMenu->AppendSeparator();
  viewMenu->Append(XRCID("reset-layout"), _("Reset Kennel Layout"),
                   _("Restore the panes to their initial layout"));
  Bind(wxEVT_MENU, &MainFrame::OnResetLayout, this, XRCID("reset-layout"));
  menuBar->Append(viewMenu, _("&View"));
}

void MainFrame::OnResetLayout(wxCommandEvent &) { m_mainView->ResetLayout(); }

namespace {
CloseAction CloseActionOfMenuId(int id) {
  if (id == XRCID("close-action-archive")) {
    return CloseAction::Archive;
  }
  if (id == XRCID("close-action-close")) {
    return CloseAction::Close;
  }
  return CloseAction::Ask;
}
} // namespace

void MainFrame::OnCloseAction(wxCommandEvent &e) {
  auto &prefs = AppManager::Get().GetPrefs();
  prefs.sessionCloseAction = CloseActionOfMenuId(e.GetId());
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not save the close action: " << st.message();
  }
}

void MainFrame::OnCloseActionUI(wxUpdateUIEvent &e) {
  e.Check(AppManager::Get().GetPrefs().sessionCloseAction ==
          CloseActionOfMenuId(e.GetId()));
}

// ---------------------------------------------------------------------------
// Finding agents on a first run
// ---------------------------------------------------------------------------

void MainFrame::StartLocalAgentScan() {
  if (AppManager::Get().GetPrefs().localAgentsScanned) {
    // Done on an earlier run. The question about WSL and the hosts is asked
    // only if it was not asked yet. After this handler: it shows a dialog.
    CallAfter(&MainFrame::OfferRemoteAgentScan);
    return;
  }
  SetActivityText(_("Looking for AI agents on this computer..."));
  StartActivityIndicator();
  AgentScan::StartLocal();
}

void MainFrame::OfferRemoteAgentScan() {
  auto &prefs = AppManager::Get().GetPrefs();
  if (prefs.remoteScanOffered) {
    return;
  }
  const auto distros = agentscan::ListWslDistros();
  const auto hosts = agentscan::ListRemoteHosts();
  if (distros.empty() && hosts.empty()) {
    return; // Nothing to offer (asked again when there is)
  }

  wxArrayString choices;
  for (const auto &distro : distros) {
    choices.Add(_("WSL: ") + distro.distro);
  }
  for (const auto &host : hosts) {
    choices.Add(
        host.name.empty() || host.name == host.address
            ? _("SSH: ") + host.address
            : wxString::Format(_("SSH: %s (%s)"), host.name, host.address));
  }

  wxMultiChoiceDialog dlg(
      this,
      _("Kennel can look for AI agents (claude, codex, kiro-cli, opencode) on "
        "these machines and define them for you.\nChoose the machines to "
        "check:"),
      _("Kennel"), choices);
  // The distros of this machine are checked; the SSH hosts are the user's to
  // choose (a jump host or a git server is not a place for agents).
  wxArrayInt preselected;
  for (size_t i = 0; i < distros.size(); ++i) {
    preselected.Add(static_cast<int>(i));
  }
  dlg.SetSelections(preselected);
  const bool accepted = dlg.ShowModal() == wxID_OK;

  // Asked once, whatever the answer.
  prefs.remoteScanOffered = true;
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not save the preferences: " << st.message();
  }
  if (!accepted) {
    return;
  }

  std::vector<agentscan::WslTarget> chosenDistros;
  std::vector<SshHostEntry> chosenHosts;
  for (int index : dlg.GetSelections()) {
    const size_t i = static_cast<size_t>(index);
    if (i < distros.size()) {
      chosenDistros.push_back(distros[i]);
    } else {
      chosenHosts.push_back(hosts[i - distros.size()]);
    }
  }
  if (chosenDistros.empty() && chosenHosts.empty()) {
    return;
  }
  SetActivityText(_("Looking for AI agents on the selected machines..."));
  StartActivityIndicator();
  AgentScan::StartRemote(std::move(chosenDistros), std::move(chosenHosts));
}

namespace {
wxString JoinNames(const wxArrayString &names) {
  wxString joined;
  for (const wxString &name : names) {
    joined << (joined.empty() ? "" : ", ") << name;
  }
  return joined;
}
} // namespace

void MainFrame::OnAgentScan(AgentScanEvent &e) {
  if (e.starting) {
    // Where the search is now: it can take a while for a host.
    SetActivityText(
        wxString::Format(_("Looking for AI agents on %s (%zu/%zu)..."), e.where,
                         e.index, e.total));
    KLOG_INFO() << "Agent scan: looking on " << e.where;
    return;
  }

  if (!e.last || e.local) {
    const wxString where = e.local ? _("this computer") : e.where;
    const wxArrayString names = AddFoundAgents(e.agents);
    KLOG_INFO() << "Agent scan: " << where << ": found " << e.agents.size()
                << ", added " << names.size();
    if (!names.empty()) {
      m_scanSummary.Add(
          wxString::Format(_("%s: added %s"), where, JoinNames(names)));
    } else if (e.agents.empty()) {
      m_scanSummary.Add(wxString::Format(_("%s: no agents found"), where));
    } else {
      m_scanSummary.Add(wxString::Format(_("%s: already defined"), where));
    }
    if (!e.last) {
      return;
    }
  }

  StopActivityIndicator();
  ClearActivityText();
  const wxString summary = wxJoin(m_scanSummary, '\n', '\0');
  m_scanSummary.Clear();
  if (e.local) {
    // Never again, even if nothing was found: the user can add agents by hand.
    auto &prefs = AppManager::Get().GetPrefs();
    prefs.localAgentsScanned = true;
    if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
      KLOG_WARN() << "Could not save the preferences: " << st.message();
    }
    SetActivityText(summary);
    // After the event: it shows a dialog.
    CallAfter(&MainFrame::OfferRemoteAgentScan);
    return;
  }
  // The scan of the selected machines is over: tell what happened.
  SetActivityText(_("Looking for AI agents: done"));
  ::wxMessageBox(summary, _("Looking for AI agents"), wxOK | wxICON_INFORMATION,
                 this);
}

wxArrayString MainFrame::AddFoundAgents(const std::vector<AgentDef> &agents) {
  auto &config = AppManager::Get().Config();
  // Where an agent runs, and what tool it is: two agents with the same place
  // and tool are the same agent, whatever they are named.
  const auto sameAgent = [](const AgentDef &a, const AgentDef &b) {
    return a.remoteHost == b.remoteHost &&
           wsl::DistroOf(a.loginShell) == wsl::DistroOf(b.loginShell) &&
           ClientKindOf(a.executable) == ClientKindOf(b.executable);
  };
  wxArrayString addedNames;
  for (const AgentDef &agent : agents) {
    const bool exists = std::any_of(
        config.agents.begin(), config.agents.end(), [&](const AgentDef &a) {
          return a.name == agent.name || sameAgent(a, agent);
        });
    if (!exists) {
      config.agents.push_back(agent);
      addedNames.Add(agent.name);
    }
  }
  const size_t added = addedNames.size();
  if (added == 0) {
    return addedNames;
  }
  if (Status st = AppManager::Get().Configs().Save(config); !st.ok()) {
    // Reload() reads the file: it would drop the agents that were just added.
    // They stay in memory for this run.
    KLOG_WARN() << "Could not save the agents: " << st.message();
  } else {
    AppManager::Get().Reload();
  }
  BuildLaunchTools();
  m_toolBar->Realize();
  PostSizeEvent();
  return addedNames;
}

void MainFrame::OnArchivedSessions(wxCommandEvent &) {
  m_mainView->GetSessionsBook()->ShowArchiveDialog();
}

// ---------------------------------------------------------------------------
// Goto Anything
// ---------------------------------------------------------------------------

void MainFrame::OnGotoAnything(wxCommandEvent &) {
  std::optional<GotoItem> chosen;
  {
    GotoAnythingDlg dlg(this);
    dlg.SetItems(CollectGotoItems());
    if (dlg.ShowModal() == wxID_OK && dlg.GetChosen() != nullptr) {
      chosen = *dlg.GetChosen();
    }
  }
  // After the dialog is gone: wx gives the focus back to the window that had it
  // before, which must not take it from the page that is shown now.
  if (chosen) {
    RunGotoItem(*chosen);
  }
}

namespace {
// The text of a menu accelerator as the user knows it. wx names the keys by
// their role; on macOS "Ctrl" is the Command key and "RawCtrl" is the Control
// key. On the other systems both are the Ctrl key.
wxString ShortcutDisplayText(wxString shortcut) {
#ifdef __WXOSX__
  shortcut.Replace("RawCtrl", "\x01"); // Keep it apart from "Ctrl"
  shortcut.Replace("Ctrl", "Cmd");
  shortcut.Replace("\x01", "Ctrl");
#else
  shortcut.Replace("RawCtrl", "Ctrl");
#endif
  return shortcut;
}
} // namespace

void MainFrame::CollectMenuItems(wxMenu *menu, const wxString &path,
                                 std::vector<GotoItem> &items) {
  // Brings the enabled state of the items up to date.
  menu->UpdateUI(this);
  for (wxMenuItem *menuItem : menu->GetMenuItems()) {
    if (menuItem->IsSeparator()) {
      continue;
    }
    const wxString text = menuItem->GetItemLabelText();
    if (menuItem->IsSubMenu()) {
      CollectMenuItems(menuItem->GetSubMenu(), path + text + " > ", items);
      continue;
    }
    if (!menuItem->IsEnabled() || menuItem->GetId() == XRCID("goto-anything")) {
      continue;
    }
    GotoItem item;
    item.kind = GotoItem::Kind::Menu;
    item.label = path + text;
    const wxString full = menuItem->GetItemLabel();
    if (const int tab = full.Find('\t'); tab != wxNOT_FOUND) {
      item.shortcut = ShortcutDisplayText(full.Mid(tab + 1));
    }
    item.menuId = menuItem->GetId();
    item.checkable = menuItem->IsCheckable();
    item.checked = menuItem->IsChecked();
    items.push_back(std::move(item));
  }
}

std::vector<GotoItem> MainFrame::CollectGotoItems() {
  std::vector<GotoItem> items;
  MainNotebook *book = m_mainView->GetSessionsBook();

  // What can be done with the page that is showing.
  if (SessionPage *active = book->GetActiveSessionPage()) {
    const Session &session = active->GetSession();
    const SessionRef ref{session.groupName, session.name};
    GotoItem restart;
    restart.kind = GotoItem::Kind::Restart;
    restart.label = active->IsPlainTerminal() ? _("Restart Terminal")
                                              : _("Restart Session");
    restart.ref = ref;
    items.push_back(std::move(restart));

    if (!active->IsPlainTerminal()) {
      for (const AgentDef &agent : active->GetReviewBuddyChoices()) {
        GotoItem review;
        review.kind = GotoItem::Kind::StartReview;
        review.label =
            wxString::Format(_("Start Review Buddy with %s"), agent.name);
        review.ref = ref;
        review.agentName = agent.name;
        items.push_back(std::move(review));
      }
      if (active->HasReviewBuddy()) {
        GotoItem stop;
        stop.kind = GotoItem::Kind::StopReview;
        stop.label = _("Close Review Buddy");
        stop.ref = ref;
        items.push_back(std::move(stop));
      }
    }
  }

  // The open pages.
  for (SessionPage *page : book->GetSessions()) {
    const Session &session = page->GetSession();
    GotoItem item;
    item.kind = GotoItem::Kind::Page;
    item.label = (page->IsPlainTerminal() ? _("Select Terminal: ")
                                          : _("Select Session: ")) +
                 session.name;
    item.ref = SessionRef{session.groupName, session.name};
    items.push_back(std::move(item));
  }
  for (FilePage *file : book->GetFiles()) {
    GotoItem item;
    item.kind = GotoItem::Kind::Page;
    item.label = _("Select File: ") +
                 (file->GetPath().empty() ? file->GetKey() : file->GetPath());
    item.ref = SessionRef{kFilesGroupName, file->GetKey()};
    items.push_back(std::move(item));
  }

  // The menu bar, as it is now: the Edit menu is in it only while a file is
  // showing, so its items are only here then.
  if (wxMenuBar *menuBar = GetMenuBar()) {
    for (size_t i = 0; i < menuBar->GetMenuCount(); ++i) {
      // The review buddy has its own entries above (its menu is made when it
      // opens).
      if (m_reviewBuddyMenu &&
          menuBar->GetMenu(i) == m_reviewBuddyMenu->GetMenu()) {
        continue;
      }
      CollectMenuItems(menuBar->GetMenu(i),
                       menuBar->GetMenuLabelText(i) + " > ", items);
    }
  }
  return items;
}

void MainFrame::RunGotoItem(const GotoItem &item) {
  MainNotebook *book = m_mainView->GetSessionsBook();
  switch (item.kind) {
  case GotoItem::Kind::Menu: {
    wxCommandEvent event{wxEVT_MENU, item.menuId};
    event.SetEventObject(this);
    if (item.checkable) {
      event.SetInt(item.checked ? 0 : 1);
    }
    GetEventHandler()->AddPendingEvent(event);
    break;
  }
  case GotoItem::Kind::Page:
    if (wxWindow *page = book->FindPage(item.ref)) {
      book->ShowPage(page);
    }
    break;
  case GotoItem::Kind::Restart:
    if (SessionPage *page =
            book->FindSession(item.ref.groupName, item.ref.name)) {
      page->CallAfter(&SessionPage::Restart);
    }
    break;
  case GotoItem::Kind::StartReview:
    if (SessionPage *page =
            book->FindSession(item.ref.groupName, item.ref.name)) {
      page->StartReviewBuddy(item.agentName);
    }
    break;
  case GotoItem::Kind::StopReview:
    if (SessionPage *page =
            book->FindSession(item.ref.groupName, item.ref.name)) {
      page->StopReviewBuddy();
    }
    break;
  }
}

namespace {
// The pane a View menu item shows or hides.
wxString PaneOfMenuId(int id) {
  if (id == XRCID("show-side-view")) {
    return "SideView";
  }
  return id == XRCID("show-output") ? wxString("Output") : wxString("Activity");
}
} // namespace

void MainFrame::OnShowPane(wxCommandEvent &e) {
  const wxString name = PaneOfMenuId(e.GetId());
  m_mainView->ShowPane(name, !m_mainView->IsPaneShown(name));
}

void MainFrame::OnShowPaneUI(wxUpdateUIEvent &e) {
  e.Check(m_mainView->IsPaneShown(PaneOfMenuId(e.GetId())));
}

void MainFrame::BuildEditMenu(wxMenuBar *menuBar) {
  // The menu is in the bar only while a file is showing.
  m_editMenu = std::make_unique<EditMenu>(this, m_mainView->GetSessionsBook());
  m_editMenu->AddTo(menuBar);
}

void MainFrame::BuildReviewBuddyMenu(wxMenuBar *menuBar) {
  // The menu is in the bar only while a session is showing, after the Jobs menu
  // (the last one of the bar now).
  m_reviewBuddyMenu =
      std::make_unique<ReviewBuddyMenu>(this, m_mainView->GetSessionsBook());
  m_reviewBuddyMenu->AddTo(menuBar,
                           menuBar->GetMenu(menuBar->GetMenuCount() - 1));
}

void MainFrame::BuildJobsMenu(wxMenuBar *menuBar) {
  auto *jobsMenu = new wxMenu();
  jobsMenu->Append(XRCID("manage-jobs"), _("Manage Jobs..."),
                   _("Create, edit, or delete timer-based jobs"));
  Bind(wxEVT_MENU, &MainFrame::OnManageJobs, this, XRCID("manage-jobs"));
  jobsMenu->AppendSeparator();
  jobsMenu->Append(XRCID("view-job-log"), _("View Job Log..."),
                   _("View the history of job runs"));
  Bind(wxEVT_MENU, &MainFrame::OnViewJobLog, this, XRCID("view-job-log"));
  menuBar->Append(jobsMenu, _("&Jobs"));
}

void MainFrame::OnManageJobs(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  EditJobsDlg dlg{this};
  if (dlg.ShowModal() == wxID_OK) {
    auto &config = AppManager::Get().Config();
    config.jobs = dlg.GetJobs();
    AppManager::Get().Configs().Save(config);
    m_jobScheduler->Reload();
  }
}

void MainFrame::OnViewJobLog(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  JobLogViewer dlg{this};
  dlg.ShowModal();
}

void MainFrame::BuildSettingsMenu(wxMenuBar *menuBar) {
  auto *settingsMenu = new wxMenu();
  settingsMenu->Append(XRCID("edit-agents"), _("Manage Agents..."),
                       _("Manage the Defined Agents"));
  settingsMenu->AppendSeparator();
  settingsMenu->Append(XRCID("edit-hosts"), _("Manage Remote Hosts..."),
                       _("Open the Remote Hosts Edit View"));
  Bind(wxEVT_MENU, &MainFrame::OnEditAgents, this, XRCID("edit-agents"));

  settingsMenu->AppendSeparator();
  settingsMenu->Append(XRCID("check-for-updates"), _("Check for Updates..."),
                       _("Check if a newer version of Kennel is available"));
  Bind(wxEVT_MENU, &MainFrame::OnCheckForUpdates, this,
       XRCID("check-for-updates"));

#ifdef __WXMAC__
  settingsMenu->AppendSeparator();
  settingsMenu->Append(wxID_ABOUT, _("&About Kennel"));
  Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
  settingsMenu->Append(wxID_EXIT); // This one will be moved to the app menu
#else
  settingsMenu->AppendSeparator();
#endif

  settingsMenu->Append(
      wxID_PREFERENCES); // on macOS, this one will be moved to the app menu
  Bind(wxEVT_MENU, &MainFrame::OnSettings, this, wxID_PREFERENCES);

  Bind(wxEVT_MENU, &MainFrame::OnEditHosts, this, XRCID("edit-hosts"));
  menuBar->Append(settingsMenu, "&Settings");
}

void MainFrame::OnEditHosts(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  EditHosts dlg{this, EditHostsMode::kEdit};
  dlg.ShowModal();
}

void MainFrame::OnSettings(wxCommandEvent &evt) {
  SettingsDlg settingsDlg{this};
  if (settingsDlg.ShowModal() == wxID_OK) {
    // Apply changes
    ThemeManager::Get().SetBlockCursor(settingsDlg.GetUseBlockCursor());

    // Save preferences
    auto &prefs = AppManager::Get().GetPrefs();
    prefs.terminalFontDesc =
        settingsDlg.GetSelectedFont().GetNativeFontInfoDesc();
    prefs.terminalTheme = settingsDlg.GetTheme();
    prefs.appTheme = settingsDlg.GetAppThemeName();
    prefs.blockCursor = settingsDlg.GetUseBlockCursor();
    prefs.blockCursor = settingsDlg.GetUseBlockCursor();
    if (prefs.terminalLoginShell != settingsDlg.GetDefaultLoginShell()) {
      // A new default: the question for a new terminal starts with it again.
      prefs.lastTerminalShell.clear();
    }
    prefs.terminalLoginShell = settingsDlg.GetDefaultLoginShell();
    prefs.terminalHomeDir = settingsDlg.GetDefaultHomeDir();
    prefs.terminalOptimizedDrawing = settingsDlg.OptimizeTerminalDrawing();
    prefs.checkForUpdatesOnStartup = settingsDlg.GetCheckForUpdatesOnStartup();
    prefs.scrollbackLines = settingsDlg.GetScrollBackLines();
    prefs.tabHasCloseButton = settingsDlg.IsCloseButtonOnTab();
    prefs.tabPosition = settingsDlg.GetTabPosition();
    prefs.tabHeight = settingsDlg.GetTabHeight();
    prefs.sideBarTabPosition = settingsDlg.GetSideBarTabPosition();
    prefs.tabOutlineColour = settingsDlg.GetTabOutlineColour();
    prefs.groupTabs = settingsDlg.GetGroupTabs();
    ApplyAppTheme(prefs.appTheme);
    AppManager::Get().SavePrefs();

    wxCommandEvent savedEvent{wxEVT_APP_PREFS_CHANGED};
    EventNotifier::Get()->AddPendingEvent(savedEvent);

  } else {
    settingsDlg.RestoreThemeAndFont();
    settingsDlg.RestoreTabHeight();
  }
}

void MainFrame::OnNewTerminal(wxCommandEvent &evt) {
  m_mainView->GetSessionsBook()->StartTerminal();
}

void MainFrame::OnNewFile(wxCommandEvent &) {
  m_mainView->GetSessionsBook()->NewFile();
}

void MainFrame::OnOpenFile(wxCommandEvent &) {
  fileactions::OpenFiles(this, m_mainView->GetSessionsBook());
}

void MainFrame::OnNewAgent(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  NewAgentWizard wizard{wxTheApp->GetTopWindow()};
  if (wizard.RunWizard(wizard.GetFirstPage())) {
    auto newAgent = wizard.GetData();
    auto &config = AppManager::Get().Config();
    auto where = std::find_if(
        config.agents.begin(), config.agents.end(),
        [&newAgent](const AgentDef &a) { return a.name == newAgent.name; });
    if (where != config.agents.end()) {
      ::wxMessageBox(_("An agent with the same name already exists"), "Kennel",
                     wxICON_WARNING | wxOK);
      return;
    }

    config.agents.push_back(newAgent);
    AppManager::Get().Configs().Save(config);
    AppManager::Get().Reload();

    BuildLaunchTools();
    m_toolBar->Realize();
    PostSizeEvent();
  }
}

void MainFrame::OnRefreshSessionUI(wxUpdateUIEvent &evt) {
  wxUnusedVar(evt);
  evt.Enable(m_mainView->GetSessionsBook()->CanRefreshCurrent());
}

void MainFrame::OnRefreshSession(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  if (m_mainView->GetSessionsBook()->CanRefreshCurrent())
    m_mainView->GetSessionsBook()->RefreshCurrentSelection();
}

void MainFrame::OnCloseAllSessions(wxCommandEvent &evt) {
  m_mainView->GetSessionsBook()->CloseAllSessions();
}

bool MainFrame::CheckIfCanStartAgent() {
  const auto &agents = AppManager::Get().Config().agents;
  if (agents.empty()) {
    // No agents
    auto answer = ::wxMessageBox(
        _("No agents are defined. Would you like to create one now?"), "Kennel",
        wxICON_QUESTION | wxYES_NO | wxCANCEL | wxYES_DEFAULT);
    if (answer == wxYES) {
      wxCommandEvent dummEvent;
      OnNewAgent(dummEvent);
    }
    return false;
  }
  return true;
}

void MainFrame::OnStartAgent(wxCommandEvent &evt) {
  if (!CheckIfCanStartAgent())
    return;
  // No agent/group preset: the dialog defaults to the first agent and the
  // "Default" group.
  m_mainView->GetSessionsBook()->StartAgent();
}

void MainFrame::OnCloseAllSessionsUI(wxUpdateUIEvent &evt) {
  evt.Enable(m_mainView->GetSessionsBook()->SessionCount() > 0);
}

void MainFrame::OnStartAgentFromToolBar(wxCommandEvent &evt) {
  if (!m_clientToolIdToName.contains(evt.GetId())) {
    return;
  }

  if (!CheckIfCanStartAgent())
    return;

  wxString clientName = m_clientToolIdToName.find(evt.GetId())->second;
  m_mainView->GetSessionsBook()->StartAgent(clientName);
}

void MainFrame::OnActivate(wxActivateEvent &event) { event.Skip(); }

void MainFrame::OnNextSession(wxCommandEvent &e) {
  wxUnusedVar(e);
  m_mainView->GetSessionsBook()->SwitchPage(true);
}

void MainFrame::OnPrevSession(wxCommandEvent &e) {
  wxUnusedVar(e);
  m_mainView->GetSessionsBook()->SwitchPage(false);
}

void MainFrame::OnPrevSessionUI(wxUpdateUIEvent &e) {
  e.Enable(m_mainView->GetSessionsBook()->PageCount() > 1);
}

void MainFrame::OnNextSessionUI(wxUpdateUIEvent &e) {
  e.Enable(m_mainView->GetSessionsBook()->PageCount() > 1);
}

void MainFrame::OnFind(wxCommandEvent &e) {
  wxUnusedVar(e);
  m_mainView->GetSessionsBook()->ShowFind();
}

namespace {
// Where Find in Files starts: the machine and the folder of what is showing.
struct SearchTarget {
  Account account;
  wxString folder;
};

SearchTarget SearchTargetOf(MainNotebook *book) {
  if (SessionPage *session = book->GetActiveSessionPage()) {
    const Account account = session->GetAccount();
    return {account, account.m_rootDir};
  }
  if (FilePage *file = book->GetActiveFilePage()) {
    const Account account = file->GetAccount();
    const wxString path = file->GetAccountPath();
    wxString folder;
    if (account.IsLocalHost()) {
      folder = wxFileName(path).GetPath();
    } else {
      folder = path.BeforeLast('/');
      if (folder.empty() && path.StartsWith("/")) {
        folder = "/"; // A file at the root
      }
    }
    return {account, folder};
  }
  return {Account::LocalHost(), wxFileName::GetHomeDir()};
}

// The text that is selected in the page that is showing (its first line), like
// Find takes it; empty when nothing is.
wxString SelectedTextOf(MainNotebook *book) {
  if (SessionPage *session = book->GetActiveSessionPage()) {
    return session->SelectedSearchText();
  }
  if (FilePage *file = book->GetActiveFilePage()) {
    return file->SelectedSearchText();
  }
  return wxString();
}
} // namespace

void MainFrame::OnFindInFiles(wxCommandEvent &) {
  OutputView *output = m_mainView->GetOutput();
  FindResultsView *results = output->GetFindInFiles();
  UiPrefs &prefs = AppManager::Get().GetPrefs();
  Account account;
  FindQuery query;
  {
    const SearchTarget target = SearchTargetOf(m_mainView->GetSessionsBook());
    FindInFilesDlg dlg(this);
    // What the user asked for last time.
    FindQuery last;
    last.pattern = prefs.findPattern;
    last.isRegex = prefs.findRegex;
    last.matchCase = prefs.findMatchCase;
    last.wholeWord = prefs.findWholeWord;
    last.include = prefs.findInclude;
    last.exclude = prefs.findExclude;
    // A selection is what the user looks for, over what was typed last time.
    if (const wxString selected = SelectedTextOf(m_mainView->GetSessionsBook());
        !selected.empty()) {
      last.pattern = selected;
    }
    dlg.SetQuery(last);
    // The folder of the page, or the last one when the page has none (and the
    // home folder of this machine for a first search here). Starts the check of
    // ripgrep on the machine, while the dialog is open.
    wxString folder = target.folder.empty() ? prefs.findFolder : target.folder;
    if (folder.empty() && target.account.IsLocalHost()) {
      folder = wxFileName::GetHomeDir();
    }
    dlg.SetTarget(target.account, folder);
    if (dlg.ShowModal() != wxID_OK) {
      return;
    }
    account = dlg.GetAccount();
    query = dlg.GetQuery();
  }
  prefs.findPattern = query.pattern;
  prefs.findRegex = query.isRegex;
  prefs.findMatchCase = query.matchCase;
  prefs.findWholeWord = query.wholeWord;
  prefs.findInclude = query.include;
  prefs.findExclude = query.exclude;
  prefs.findFolder = query.root;
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not save the preferences: " << st.message();
  }
  output->SelectFindInFiles();
  m_mainView->ShowPane("Output", true);
  if (const Status started = results->Search(account, query); !started.ok()) {
    wxMessageBox(started.message(), _("Find in Files"), wxOK | wxICON_ERROR,
                 this);
  }
}

void MainFrame::OnFindNext(wxCommandEvent &e) {
  m_mainView->GetSessionsBook()->FindNext(e.GetId() == XRCID("find-next"));
}

void MainFrame::OnFindUI(wxUpdateUIEvent &e) {
  e.Enable(m_mainView->GetSessionsBook()->CanFind());
}

void MainFrame::OnGotoLine(wxCommandEvent &) {
  m_mainView->GetSessionsBook()->GotoLine();
}

void MainFrame::OnGotoLineUI(wxUpdateUIEvent &e) {
  e.Enable(m_mainView->GetSessionsBook()->CanGotoLine());
}

void MainFrame::OnEditAgents(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  EditAgentsDlg editAgents{this};
  if (editAgents.ShowModal() == wxID_OK) {
    auto &config = AppManager::Get().Config();
    config.agents = editAgents.GetAgents();
    AppManager::Get().Configs().Save(config);
    AppManager::Get().Reload();
    BuildLaunchTools();
    m_toolBar->Realize();
    PostSizeEvent();
  }
}

void MainFrame::OnAbout(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  AboutDialog dlg(this);
  dlg.ShowModal();
}

void MainFrame::OnCheckForUpdates(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  CheckForUpdates(/*silent=*/false);
}

void MainFrame::CheckForUpdates(bool silent) {
  m_updateChecker = std::make_unique<UpdateChecker>();
  m_updateChecker->Check(
      kAppVersion,
      [this, silent](const UpdateCheckResult &result) {
        if (result.updateAvailable) {
          const wxString msg =
              wxString::Format(_("A new version of Kennel is available: %s\n\n"
                                 "Open the download page?"),
                               result.latestVersion);
          if (::wxMessageBox(msg, "Kennel", wxICON_INFORMATION | wxYES_NO,
                             this) == wxYES) {
            ::wxLaunchDefaultBrowser(result.downloadUrl);
          }
        } else if (!silent) {
          ::wxMessageBox(_("You are running the latest version of Kennel."),
                         "Kennel", wxICON_INFORMATION | wxOK, this);
        }
      },
      [this, silent](const wxString &error) {
        KLOG_WARN() << "Update check failed: " << error;
        if (!silent) {
          ::wxMessageBox(
              wxString::Format(_("Could not check for updates: %s"), error),
              "Kennel", wxICON_WARNING | wxOK, this);
        }
      });
}

void MainFrame::OnRenameItemUI(wxUpdateUIEvent &event) {
  bool enable =
      m_mainView != nullptr && !m_mainView->GetSessionsBook()->IsEmpty();
  event.Enable(enable);
}

void MainFrame::OnRenameItem(wxCommandEvent &event) {
  wxUnusedVar(event);
  CHECK_NOT_NULL_RETURN(m_mainView);
  m_mainView->GetSessionsBook()->RenameItem();
}

MainFrame *GetMainFrame() { return mainFrame; }
MainView *GetMainView() { return GetMainFrame()->GetMainView(); }
wxEvtHandler *GetMainViewEventHandler() {
  return GetMainView()->GetEventHandler();
}

bool MainFrame::IsWindowActive(const SessionPage *win) const {
  CHECK_NOT_NULL_RETURN_FALSE(win);
  return m_mainView->GetSessionsBook()->GetActiveSessionPage() == win;
}
