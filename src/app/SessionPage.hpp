#ifndef SESSIONPAGE_HPP
#define SESSIONPAGE_HPP
#include "UI.hpp"
#include "app/AcceleratorInterceptor.h"
#include "core/ActivityMonitor.h"
#include "core/AppPaths.h"
#include "core/Config.h"
#include "core/Workspace.h"
#include <optional>

#include "terminal_theme.h"

#include <functional>
#include <memory>

class wxTerminalViewCtrl;
class wxTerminalEvent;

enum class SessionStatus { Starting, Running, Idle, Exited, Error };

class SessionPage : public SessionBasePage {
public:
  using StatusChangedFn = std::function<void(SessionStatus)>;

  SessionPage(wxBookCtrlBase *parent, std::optional<AgentDef> agent,
              Session session, bool resume = false);
  ~SessionPage() override;

  SessionStatus Status() const { return m_status; }
  const Session &GetSession() const { return m_session; }
  Session &GetSession() { return m_session; }
  inline bool IsPlainTerminal() const { return !m_agent.has_value(); }
  void Restart();
  wxTerminalViewCtrl *GetTerminal() { return m_terminal; }
  void ApplyTheme(const wxTerminalTheme &theme);

  bool IsActive() const;
  void SetDefaultSessionName(const wxString &name);

private:
  void CreateTerminal();
  void SetStatus(SessionStatus status);
  void OnTerminated(wxTerminalEvent &evt);
  void OnTitleChanged(wxTerminalEvent &evt);
  void OnTerminalLink(wxTerminalEvent &evt);
  void OnParentPageChanged(wxBookCtrlEvent &event);
  void ApplyTitle();

  wxBookCtrlBase *m_parentBook{nullptr};
  AppPaths m_paths;
  std::optional<AgentDef> m_agent{std::nullopt};
  Session m_session;
  bool m_resume = false;

  wxTerminalViewCtrl *m_terminal{nullptr};
  std::unique_ptr<ActivityMonitor> m_monitor;
  SessionStatus m_status = SessionStatus::Starting;
  wxString m_defaultTitle;
  wxString m_terminalTitle;
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor{nullptr};
};

wxDECLARE_EVENT(wxEVT_SESSION_IDLE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SESSION_ACTIVE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SESSION_EXITED, wxCommandEvent);
#endif // SESSIONPAGE_HPP
