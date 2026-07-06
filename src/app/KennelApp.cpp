#include <wx/app.h>
#include <wx/log.h>

#include "app/MainFrame.h"
#include "core/AppManager.h"
#include "core/AppPaths.h"
#include "core/CrashHandler.h"
#include "core/Logger.h"

// wxWidgets application entry point. Creates and shows the main window.
class KennelApp : public wxApp {
public:
  bool OnInit() override {
    SetAppearance(Appearance::Dark);

    // Resolve ~/.kennel and create its directory tree before anything
    // else touches config/workspace/log files.
    const AppPaths paths = AppPaths::Default();
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
    Logger::Get().SetLevel(LogLevel::kInfo);
    KLOG_INFO() << "Kennel starting; data dir: " << paths.Root();

    // Install crash handler before any further initialization so that even
    // early failures produce a backtrace in ~/.kennel/logs/crash_<epoch>.log.
    CrashHandler::Install(paths.LogsDir().GetPath());

    // Load config, workspace, and UI prefs into the process-wide AppManager.
    // UI code reaches all of these via AppManager::Get().
    AppManager::Get().Initialize(paths);

    auto *frame = new MainFrame();
    frame->Show(true);
    SetTopWindow(frame);
    return true;
  }
};

wxIMPLEMENT_APP(KennelApp);
