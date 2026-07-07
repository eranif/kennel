#include "app/MainFrame.h"

#include "SettingsDlg.hpp"
#include "app/AboutDialog.hpp"
#include "app/AssetBootstrap.h"
#include "app/EditAgentsDlg.hpp"
#include "app/EditHosts.hpp"
#include "app/NewAgentWizard.hpp"
#include "core/AdapterRegistry.h"
#include "core/AppManager.h"
#include "core/Logger.h"
#include "core/Version.h"

#include "terminal_view.h"
#include <algorithm>
#include <wx/app.h>
#include <wx/artprov.h>
#include <wx/bmpbndl.h>
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
  CreateStatusBar(2);
  int widths[] = {-1, 50};
  SetStatusWidths(2, widths);
  SetStatusText(wxString::Format("%s %s", kAppName, kAppVersion), 0);

  m_statusIndicator = new wxActivityIndicator(GetStatusBar());
  wxRect fieldRect;
  GetStatusBar()->GetFieldRect(1, fieldRect);
  m_statusIndicator->SetSize(fieldRect.GetWidth() - 4,
                             fieldRect.GetHeight() - 4);
  m_statusIndicator->Move(fieldRect.x + 2, fieldRect.y + 2);
  m_statusIndicator->Hide();

  KLOG_INFO() << "Creating main frame";
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

  // Restore the persisted window geometry. Size always applies; position only
  // when previously saved (x/y == -1 means "let the platform place it"). If the
  // window was maximized last time, maximize after setting the restore bounds.
  const WindowGeometry &g = AppManager::Get().GetPrefs().window;
  const wxPoint pos =
      (g.x >= 0 && g.y >= 0) ? wxPoint(g.x, g.y) : wxDefaultPosition;
  if (g.width > 0 && g.height > 0) {
    SetSize(wxRect(pos, wxSize(g.width, g.height)));
  } else if (pos != wxDefaultPosition) {
    Move(pos);
  }
  if (g.maximized) {
    Maximize(true);
  }

  Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
  Bind(wxEVT_ACTIVATE, &MainFrame::OnActivate, this);

  // Rebuild any sessions persisted in workspace.json (resuming where possible).
  m_mainView->RestoreSessions();
  mainFrame = this;
}

MainFrame::~MainFrame() {
  mainFrame = nullptr;
  Unbind(wxEVT_ACTIVATE, &MainFrame::OnActivate, this);
}

void MainFrame::OnClose(wxCloseEvent &evt) {
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
  if (Status st = AppManager::Get().SavePrefs(); !st.ok()) {
    KLOG_WARN() << "Could not persist window geometry: " << st.message();
  }

  // No save-on-exit dance: sessions resume via the client's own
  // --resume/--continue, so there is nothing to flush before quitting.
  evt.Skip(); // continue with the default close (destroys the frame)
}

void MainFrame::HandleCtrlTabNavigation(wxKeyEvent &evt) {
  if (m_mainView->GetActiveTerminal() == nullptr) {
    evt.Skip();
    return;
  }

  if (evt.GetId() == XRCID("tab-nav-right")) {
    // Navigating right
    m_mainView->SelectSession(true);
  } else if (evt.GetId() == XRCID("tab-nav-left")) {
    // Navigating left
    m_mainView->SelectSession(false);
  } else {
    // Unsupported
    evt.Skip();
  }
}

void MainFrame::BuildToolBar() {
#ifdef __WXMSW__
  m_toolBar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxAUI_TB_VERTICAL | wxAUI_TB_PLAIN_BACKGROUND);
  GetSizer()->Insert(0, m_toolBar, wxSizerFlags(0).Expand());
#elif defined(__WXGTK__)
  m_toolBar = CreateToolBar(wxTB_HORIZONTAL | wxTB_NODIVIDER);
#else
  m_toolBar = CreateToolBar(wxTB_HORIZONTAL);
#endif

  m_toolBar->SetToolBitmapSize(Bitmaps::GetToolBarIconSize());

  auto &bmps = AppManager::Get().GetBitmaps();

  Bind(wxEVT_TOOL, &MainFrame::OnRestartSession, this, wxID_REFRESH);
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnRestartSessionUI, this, wxID_REFRESH);
  Bind(wxEVT_TOOL, &MainFrame::OnStartAgent, this, XRCID("start-agent"));
  Bind(wxEVT_TOOL, &MainFrame::OnNewTerminal, this, XRCID("start-terminal"));

  // Static content first...
  m_toolBar->AddTool(XRCID("start-agent"), _("Start a New Agent"),
                     bmps.Get("agent", true), _("Start a New Agent"));
  m_toolBar->AddTool(XRCID("start-terminal"), _("Start a New Terminal"),
                     bmps.Get("terminal", true), _("Start a New Terminal"));
  m_toolBar->AddTool(wxID_REFRESH, _("Restart"), bmps.Get("restart", true),
                     _("Refresh the Current Agent"));
  m_toolBar->AddSeparator();
  BuildLaunchTools();
  m_toolBar->Realize();
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
    m_toolBar->AddTool(toolId, agent->name, icon,
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
  fileMenu->Append(XRCID("start-agent"), _("&Start Agent...\tCtrl+T"),
                   _("Start a new agent session"));
  Bind(wxEVT_MENU, &MainFrame::OnStartAgent, this, XRCID("start-agent"));
  fileMenu->Append(XRCID("start-terminal"), _("New Terminal\tCtrl+E"),
                   _("Start GlyphT Terminal"));
  Bind(wxEVT_MENU, &MainFrame::OnNewTerminal, this, XRCID("start-terminal"));
  fileMenu->Append(wxID_REFRESH, _("&Refresh Current Selection\tF5"),
                   _("Refresh the Current Selection"));
  Bind(wxEVT_MENU, &MainFrame::OnRestartSession, this, wxID_REFRESH);
  fileMenu->AppendSeparator();
  fileMenu->Append(wxID_NEW, _("Configure &New Agent...\tCtrl+N"),
                   _("Define a New Agent"));
  Bind(wxEVT_MENU, &MainFrame::OnNewAgent, this, wxID_NEW);
  fileMenu->AppendSeparator();
  fileMenu->Append(wxID_CLOSE_ALL, _("Close All Sessions"),
                   _("Close every session in every group"));
  Bind(wxEVT_MENU, &MainFrame::OnCloseAllSessions, this, wxID_CLOSE_ALL);
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnCloseAllSessionsUI, this, wxID_CLOSE_ALL);

#ifndef __WXMAC__
  fileMenu->AppendSeparator();
  fileMenu->Append(wxID_EXIT);
#endif
  menuBar->Append(fileMenu, "&File");

  BuildEditMenu(menuBar);
  BuildSearchMenu(menuBar);
  BuildSettingsMenu(menuBar);
  SetMenuBar(menuBar);

  Bind(wxEVT_MENU, [this](wxCommandEvent &) { Close(); }, wxID_EXIT);
}

void MainFrame::BuildSearchMenu(wxMenuBar *menuBar) {
  auto *searchMenu = new wxMenu();
#ifdef __WXMAC__
  searchMenu->Append(wxID_FORWARD, _("Select Next Session\tCtrl-RIGHT"),
                     _("Select the next session"));
  searchMenu->Append(wxID_BACKWARD, _("Select Previous Session\tCtrl-LEFT"),
                     _("Select the previous session"));
#else
  searchMenu->Append(wxID_FORWARD, _("Select Next Session\tAlt-RIGHT"),
                     _("Select the next session"));
  searchMenu->Append(wxID_BACKWARD, _("Select Previous Session\tAlt-LEFT"),
                     _("Select the previous session"));
#endif

  Bind(wxEVT_MENU, &MainFrame::OnNextSession, this, wxID_FORWARD);
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnPrevSessionUI, this, wxID_BACKWARD);
  Bind(wxEVT_MENU, &MainFrame::OnPrevSession, this, wxID_BACKWARD);
  Bind(wxEVT_UPDATE_UI, &MainFrame::OnNextSessionUI, this, wxID_FORWARD);
  menuBar->Append(searchMenu, _("Search"));
}

void MainFrame::BuildEditMenu(wxMenuBar *menuBar) {
  auto *editMenu = new wxMenu();
  editMenu->Append(XRCID("rename-selection"), _("Rename Selection\tF2"),
                   _("Rename the selected group"));
  menuBar->Append(editMenu, "&Edit");
  Bind(wxEVT_MENU, &MainFrame::OnRenameItem, this, XRCID("rename-selection"));
  Bind(
      wxEVT_UPDATE_UI,
      [this](wxUpdateUIEvent &e) {
        e.Enable((m_mainView->IsSelectionSessionGroup() &&
                  m_mainView->GetSelectedItemText() != _("Default")) ||
                 m_mainView->IsSelectionTerminal());
      },
      XRCID("rename-selection"));
}

void MainFrame::BuildSettingsMenu(wxMenuBar *menuBar) {
  auto *settingsMenu = new wxMenu();
  settingsMenu->Append(XRCID("edit-agents"), _("Manage Agents..."),
                       _("Manage the Defined Agents"));
  settingsMenu->AppendSeparator();
  settingsMenu->Append(XRCID("edit-hosts"), _("Manage Remote Hosts..."),
                       _("Open the Remote Hosts Edit View"));
  Bind(wxEVT_MENU, &MainFrame::OnEditAgents, this, XRCID("edit-agents"));

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

  settingsMenu->Append(XRCID("terminal-optimized-drawing"),
                       _("Enable Optimized Rendering"),
                       _("Enable Terminal Optimized Drawing"), wxITEM_CHECK);

  Bind(wxEVT_MENU, &MainFrame::OnEditHosts, this, XRCID("edit-hosts"));
  Bind(wxEVT_MENU, &MainFrame::OnEnableOptimizedDrawing, this,
       XRCID("terminal-optimized-drawing"));
  Bind(
      wxEVT_UPDATE_UI,
      [](wxUpdateUIEvent &e) {
        if (wxTerminalViewCtrl::IsOpenGLEnabled()) {
          AppManager::Get().GetPrefs().terminalOptimizedDrawing = false;
          e.Enable(false);
          return;
        }
        e.Check(AppManager::Get().GetPrefs().terminalOptimizedDrawing);
      },
      XRCID("terminal-optimized-drawing"));
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

    m_mainView->ApplyFont(settingsDlg.GetSelectedFont());
    m_mainView->ApplyTheme(settingsDlg.GetTheme());

    // Save preferences
    auto &prefs = AppManager::Get().GetPrefs();
    prefs.terminalFontDesc =
        settingsDlg.GetSelectedFont().GetNativeFontInfoDesc();
    prefs.terminalTheme = settingsDlg.GetTheme();
    prefs.blockCursor = settingsDlg.GetUseBlockCursor();
    prefs.terminalLoginShell = settingsDlg.GetDefaultLoginShell();
    prefs.terminalHomeDir = settingsDlg.GetDefaultHomeDir();
    AppManager::Get().SavePrefs();
  }
}

void MainFrame::OnEnableOptimizedDrawing(wxCommandEvent &evt) {
  AppManager::Get().GetPrefs().terminalOptimizedDrawing = evt.IsChecked();
  if (wxTerminalViewCtrl::IsOpenGLEnabled()) {
    AppManager::Get().GetPrefs().terminalOptimizedDrawing = false;
  }
  AppManager::Get().SavePrefs();
  m_mainView->ApplyOptimizedDrawing();
}

void MainFrame::OnNewTerminal(wxCommandEvent &evt) {
  m_mainView->StartTerminal();
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

void MainFrame::OnRestartSessionUI(wxUpdateUIEvent &evt) {
  wxUnusedVar(evt);
  evt.Enable(m_mainView->CanRefreshCurrent());
}

void MainFrame::OnRestartSession(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  m_mainView->RefreshCurrentSelection();
}

void MainFrame::OnCloseAllSessions(wxCommandEvent &evt) {
  m_mainView->CloseAllSessions();
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
  m_mainView->StartAgent();
}

void MainFrame::OnCloseAllSessionsUI(wxUpdateUIEvent &evt) {
  evt.Enable(m_mainView->SessionCount() > 0);
}

void MainFrame::OnStartAgentFromToolBar(wxCommandEvent &evt) {
  if (!m_clientToolIdToName.contains(evt.GetId())) {
    return;
  }

  if (!CheckIfCanStartAgent())
    return;

  wxString clientName = m_clientToolIdToName.find(evt.GetId())->second;
  m_mainView->StartAgent(clientName);
}

void MainFrame::OnActivate(wxActivateEvent &event) { event.Skip(); }

void MainFrame::OnNextSession(wxCommandEvent &e) {
  wxUnusedVar(e);
  m_mainView->SelectSession(true);
}

void MainFrame::OnPrevSession(wxCommandEvent &e) {
  wxUnusedVar(e);
  m_mainView->SelectSession(false);
}

void MainFrame::OnPrevSessionUI(wxUpdateUIEvent &e) {
  e.Enable(m_mainView->SessionCount() > 1);
}

void MainFrame::OnNextSessionUI(wxUpdateUIEvent &e) {
  e.Enable(m_mainView->SessionCount() > 1);
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

void MainFrame::OnRenameItem(wxCommandEvent &event) {
  wxUnusedVar(event);
  auto item = m_mainView->GetTree()->GetSelection();
  if (m_mainView->IsSelectionSessionGroup()) {
    m_mainView->RenameGroup(item);
  } else if (m_mainView->IsSelectionTerminal()) {
    m_mainView->RenameTerminal(item);
  }
}

MainFrame *GetMainFrame() { return mainFrame; }
