#include "SessionPage.hpp"

#include "ThemeManager.h"
#include "core/AppManager.h"
#include "core/ClientAdapter.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include "terminal_event.h"
#include "terminal_view.h"

#include <wx/frame.h>
#include <wx/utils.h>

#include <map>
#include <optional>
#include <wx/choicdlg.h>
#include <wx/msgdlg.h>

wxDEFINE_EVENT(wxEVT_SESSION_IDLE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_ACTIVE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_EXITED, wxCommandEvent);

namespace {

std::optional<wxTerminalViewCtrl::EnvironmentList>
BuildEnvironment(const std::map<wxString, wxString> &overrides) {
  if (overrides.empty()) {
    return std::nullopt;
  }

  wxEnvVariableHashMap env;
  wxGetEnvMap(&env);
  for (const auto &[key, value] : overrides) {
    env[key] = value;
  }

  wxTerminalViewCtrl::EnvironmentList list;
  list.reserve(env.size());
  for (const auto &entry : env) {
    wxString line;
    line << entry.first << "=" << entry.second;
    list.push_back(line.ToStdString(wxConvUTF8));
  }
  return list;
}
} // namespace

SessionPage::SessionPage(wxBookCtrlBase *parent, std::optional<AgentDef> agent,
                         Session session, bool resume)
    : SessionBasePage(parent), m_parentBook{parent},
      m_paths(AppManager::Get().Paths()), m_agent(std::move(agent)),
      m_session(std::move(session)), m_resume(resume) {
  SetDefaultSessionName(m_session.name);
  m_parentBook->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED,
                     &SessionPage::OnParentPageChanged, this);
  CreateTerminal();
}

SessionPage::~SessionPage() {
  m_parentBook->Unbind(wxEVT_BOOKCTRL_PAGE_CHANGED,
                       &SessionPage::OnParentPageChanged, this);
}

bool SessionPage::IsActive() const {
  return m_parentBook->GetCurrentPage() == this;
}

void SessionPage::SetDefaultSessionName(const wxString &name) {
  m_defaultTitle = MakeAppTitle(name, m_session.agentName);
  m_session.name = name;
  ApplyTitle();
}

void SessionPage::CreateTerminal() {
  std::vector<wxString> commands;
  std::optional<wxTerminalViewCtrl::EnvironmentList> env{std::nullopt};
  if (m_agent) {
    commands = BuildCommandLine(*m_agent, m_session.workingDir, m_resume);
    env = BuildEnvironment(m_agent->env);
  }

  std::optional<wxString> cwd;
  if (!m_session.workingDir.empty()) {
    cwd = m_session.workingDir;
  }

  const auto &prefs = AppManager::Get().GetPrefs();
  // Use the default login shell
  wxString shellCommand = prefs.terminalLoginShell;

  if (m_session.plainTerminal) {
    // For plain terminals, we use the following logic to determine the
    // terminal: Assume the default terminal. If we have multiple terminals,
    // prompt the user to pick one (we set the default terminal as the default
    // terminal in the selection dialog).
    const auto &shells = ::FindShells().shells;
    if (shells.size() > 1) {
      wxArrayString shell_names_arr;
      std::unordered_map<wxString, wxString> m;
      wxString selectedShellName;
      for (const auto &[shell_name, shell_cmd] : shells) {
        if (shell_cmd == prefs.terminalLoginShell) {
          selectedShellName = shell_name;
        }
        shell_names_arr.push_back(shell_name);
        m.insert({shell_name, shell_cmd});
      }

      int selection = shell_names_arr.Index(selectedShellName);
      wxString selectionShell = ::wxGetSingleChoice(
          _("Choose a Login Shell:"), "Kennel", shell_names_arr, selection);
      if (selectionShell.empty()) {
        return; // user cancelled
      }
      shellCommand = m[selectionShell];
    }
  }

  if (m_agent) {
    if (!m_agent->loginShell.empty()) {
      // This agent has a custom shell -> use it
      shellCommand = m_agent->loginShell;
    }
    if (!m_agent->remoteHost.empty()) {
      KLOG_INFO() << "Launching remote session: " << m_session.name;
      for (const auto &cmd : commands) {
        KLOG_INFO() << " > " << cmd;
      }
      cwd.reset();
      env.reset();
    } else {
      if (!m_session.workingDir.empty()) {
        wxFileName::Mkdir(m_session.workingDir, wxS_DIR_DEFAULT,
                          wxPATH_MKDIR_FULL);
      }
      KLOG_INFO() << "Launching local session '" << m_session.name
                  << "': " << commands[0]
                  << (cwd ? wxString(" (cwd=" + *cwd + ")") : wxString());
    }
  }

  m_monitor = std::make_unique<ActivityMonitor>(
      [this]() { SetStatus(SessionStatus::Running); },
      [this]() { SetStatus(SessionStatus::Idle); });

  shellCommand.Replace(
      "%WORKING_DIRECTORY%",
      (m_session.workingDir.empty() ? "~" : m_session.workingDir));

  KLOG_INFO() << "Running shell: " << shellCommand;
  m_terminal = new wxTerminalViewCtrl(this, shellCommand, env, cwd);
  m_acceleratorInterceptor =
      std::make_unique<AcceleratorInterceptor>(m_terminal);

  GetSizer()->Add(m_terminal, wxSizerFlags(1).Border(wxALL, 5).Expand());
  GetSizer()->Layout();
  m_terminal->Bind(wxEVT_TERMINAL_TEXT_LINK, &SessionPage::OnTerminalLink,
                   this);
  for (const auto &cmd : commands) {
    m_terminal->SendCommand(cmd);
  }

  m_terminal->EnableSafeDrawing(
      !AppManager::Get().GetPrefs().terminalOptimizedDrawing);
  m_terminal->SetOutputCallback(
      [this](const std::string &chunk) { m_monitor->OnOutput(chunk); });
  m_terminal->Bind(wxEVT_TERMINAL_TERMINATED, &SessionPage::OnTerminated, this);
  m_terminal->Bind(wxEVT_TERMINAL_TITLE_CHANGED, &SessionPage::OnTitleChanged,
                   this);

  if (auto theme = ThemeManager::Get().ActiveTheme()) {
    m_terminal->SetTheme(*theme);
  }
  m_terminal->EnsureStarted();
  SetStatus(SessionStatus::Running);
}

void SessionPage::Restart() {
  KLOG_INFO() << "Restarting session '" << m_session.name << "'";
  if (m_terminal != nullptr) {
    GetSizer()->Detach(m_terminal);
    m_terminal->Destroy();
    m_terminal = nullptr;
    m_acceleratorInterceptor.reset();
  }
  m_status = SessionStatus::Starting;
  m_resume = true;
  CallAfter(&SessionPage::CreateTerminal);
}

void SessionPage::OnTerminated(wxTerminalEvent &evt) {
  evt.Skip();
  SetStatus(SessionStatus::Exited);
}

void SessionPage::OnTitleChanged(wxTerminalEvent &evt) {
  evt.Skip();
  m_terminalTitle = MakeAppTitle(evt.GetTitle(), m_session.agentName);
  ApplyTitle();
}

void SessionPage::SetStatus(SessionStatus status) {
  if (m_status == status) {
    return;
  }
  m_status = status;
  switch (m_status) {
  case SessionStatus::Exited: {
    wxCommandEvent e{wxEVT_SESSION_EXITED};
    e.SetString(m_session.name);
    GetParent()->GetEventHandler()->AddPendingEvent(e);
    break;
  }
  case SessionStatus::Running: {
    wxCommandEvent e{wxEVT_SESSION_ACTIVE};
    e.SetString(m_session.name);
    GetParent()->GetEventHandler()->AddPendingEvent(e);
    break;
  }
  case SessionStatus::Idle: {
    wxCommandEvent e{wxEVT_SESSION_IDLE};
    e.SetString(m_session.name);
    GetParent()->GetEventHandler()->AddPendingEvent(e);
    break;
  }
  default:
    break;
  }
}

void SessionPage::OnTerminalLink(wxTerminalEvent &evt) {
  wxString text = evt.GetClickedText().Lower();
  if (text.StartsWith("http://") || text.StartsWith("https://")) {
    ::wxLaunchDefaultBrowser(evt.GetClickedText());
  }
}

void SessionPage::ApplyTheme(const wxTerminalTheme &theme) {
  GetTerminal()->SetTheme(theme);
}

void SessionPage::ApplyTitle() {
  if (IsActive()) {
    auto *frame = dynamic_cast<wxFrame *>(wxTheApp->GetTopWindow());
    frame->SetLabel(m_terminalTitle.empty() ? m_defaultTitle : m_terminalTitle);
  }
}

void SessionPage::OnParentPageChanged(wxBookCtrlEvent &event) {
  event.Skip();
  ApplyTitle();
}
