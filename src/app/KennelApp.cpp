#include <wx/app.h>
#include <wx/cmdline.h>
#include <wx/log.h>

#include "app/AppTheme.h"
#include "app/AssetBootstrap.h"
#include "app/KennelManager.hpp"
#include "app/MainFrame.h"
#include "core/AppManager.h"
#include "core/AppPaths.h"
#include "core/CrashHandler.h"
#include "core/Globals.hpp"
#include "core/Logger.h"
#include "core/PromptFileRemover.h"
#include "core/lsp/LspManager.h"

// wxWidgets application entry point. Creates and shows the main window.
class KennelApp : public wxApp {
public:
  int OnExit() override {
    SetKennelManager(nullptr);
    // The windows are gone: a page that went may have left a remote file to
    // delete (PromptFileRemover). Not longer than a few seconds: a host that
    // does not answer does not keep Kennel open.
    PromptFileRemover::Get().Flush(std::chrono::seconds(5));
    return wxApp::OnExit();
  }

  void OnInitCmdLine(wxCmdLineParser &parser) override {
    wxApp::OnInitCmdLine(parser);
    parser.AddOption(
        "d", wxEmptyString,
        "Home directory to use instead of the real user home; Kennel data "
        "is stored under <dir>/.kennel. Lets multiple instances run "
        "side by side with separate data.",
        wxCMD_LINE_VAL_STRING);
    parser.AddOption("l", "log-level",
                     "How much to log: trace, debug, info (the default), warn "
                     "or error.",
                     wxCMD_LINE_VAL_STRING);
    parser.AddSwitch("", "stdout",
                     "Also write the log to the console (the standard "
                     "output).");
  }

  bool OnCmdLineParsed(wxCmdLineParser &parser) override {
    if (!wxApp::OnCmdLineParsed(parser)) {
      return false;
    }
    parser.Found("d", &m_homeOverride);
    wxString level;
    if (parser.Found("log-level", &level) &&
        !Logger::ParseLevel(level, &m_logLevel)) {
      parser.Usage();
      return false;
    }
    if (parser.Found("stdout")) {
      Logger::Get().SetStdout(true);
    }
    return true;
  }

  bool OnInit() override {
    if (!wxApp::OnInit()) {
      return false;
    }
    // Resolve ~/.kennel (or <-d value>/.kennel) and create its directory
    // tree before anything else touches config/workspace/log files.
    const AppPaths paths = m_homeOverride.empty()
                               ? AppPaths::Default()
                               : AppPaths::WithHome(m_homeOverride);
    wxString error;
    if (!paths.EnsureDirectories(&error)) {
      // The log file lives under the very directory we failed to make,
      // so fall back to a dialog for this one case.
      wxLogError("Kennel could not initialize its data directory:\n%s", error);
      return false;
    }

    // Bring up file logging now that ~/.kennel/logs exists.
    wxFileName logFile = paths.LogsDir();
    logFile.SetFullName("kennel.log");
    Logger::Get().SetLogFile(logFile.GetFullPath());
    Logger::Get().SetLevel(m_logLevel);
    KLOG_INFO() << "Kennel starting; data dir: " << paths.Root();

    // Install crash handler before any further initialization so that even
    // early failures produce a backtrace in ~/.kennel/logs/crash_<epoch>.log.
    CrashHandler::Install(paths.LogsDir().GetPath());

    // Before any window or font is created.
    LoadBundledFonts();

    // Load config, workspace, and UI prefs into the process-wide AppManager.
    // UI code reaches all of these via AppManager::Get().
    AppManager::Get().Initialize(paths);

    // The core reaches what is live in the application through it.
    SetKennelManager(&m_manager);

    // Before the first page is made: it follows the pages that are open.
    LspManager::Get().Initialize();

    // Before the first window is created.
    ApplyAppTheme(AppManager::Get().GetPrefs().appTheme);

    static wxLogNull noLog;
    auto *frame = new MainFrame();
    frame->Show(true);
    SetTopWindow(frame);
    return true;
  }

private:
  KennelManager m_manager;
  wxString m_homeOverride;
  LogLevel m_logLevel{LogLevel::kInfo};
};

wxIMPLEMENT_APP(KennelApp);
