#pragma once

#include <wx/event.h>
#include <wx/string.h>

#include <functional>

// Result of a completed update check.
struct UpdateCheckResult {
  bool updateAvailable{false};
  wxString latestVersion;
  wxString downloadUrl;
};

// Fetches https://kennel.codelite.org/kennel-releases.json via wxWebRequest
// and compares the platform-specific version against the running app's
// version. Async: Check() returns immediately and invokes the callback (on
// the UI thread) once the request completes, whether it found an update,
// found none, or failed. A failure calls onError instead of onResult.
//
// Owns no persistent state; construct one per check (or keep one alive for
// the app's lifetime — either works since each Check() creates a fresh
// wxWebRequest).
class UpdateChecker : public wxEvtHandler {
public:
  using ResultFn = std::function<void(const UpdateCheckResult &)>;
  using ErrorFn = std::function<void(const wxString &error)>;

  // Starts the async request. `currentVersion` is compared against the
  // release JSON's version for the running platform.
  void Check(const wxString &currentVersion, ResultFn onResult,
             ErrorFn onError);

private:
  void OnRequestState(class wxWebRequestEvent &evt);

  ResultFn m_onResult;
  ErrorFn m_onError;
  wxString m_currentVersion;
};

// Returns true if `a` denotes a newer version than `b`. Compares dotted
// numeric components (e.g. "1.2.0" vs "1.10.0"); a missing component is
// treated as 0.
bool IsVersionNewer(const wxString &a, const wxString &b);
