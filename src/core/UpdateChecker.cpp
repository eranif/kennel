#include "core/UpdateChecker.h"

#include "core/JsonUtil.h"
#include "core/Logger.h"

#include <wx/tokenzr.h>
#include <wx/webrequest.h>

namespace {

// Key into the release JSON's per-platform object.
const char *PlatformKey() {
#if defined(__WXMAC__)
  return "macOS";
#elif defined(__WXMSW__)
  return "windows";
#else
  return "linux";
#endif
}

constexpr const char *kReleasesUrl =
    "https://kennel.codelite.org/kennel-releases.json";

} // namespace

bool IsVersionNewer(const wxString &a, const wxString &b) {
  wxStringTokenizer ta{a, "."};
  wxStringTokenizer tb{b, "."};
  while (ta.HasMoreTokens() || tb.HasMoreTokens()) {
    long va = 0, vb = 0;
    if (ta.HasMoreTokens())
      ta.GetNextToken().ToLong(&va);
    if (tb.HasMoreTokens())
      tb.GetNextToken().ToLong(&vb);
    if (va != vb)
      return va > vb;
  }
  return false;
}

void UpdateChecker::Check(const wxString &currentVersion, ResultFn onResult,
                          ErrorFn onError) {
  m_currentVersion = currentVersion;
  m_onResult = std::move(onResult);
  m_onError = std::move(onError);

  wxWebRequest request =
      wxWebSession::GetDefault().CreateRequest(this, kReleasesUrl);
  if (!request.IsOk()) {
    if (m_onError)
      m_onError(_("Could not create the update-check request"));
    return;
  }

  Bind(wxEVT_WEBREQUEST_STATE, &UpdateChecker::OnRequestState, this);
  request.Start();
}

void UpdateChecker::OnRequestState(wxWebRequestEvent &evt) {
  switch (evt.GetState()) {
  case wxWebRequest::State_Completed: {
    wxWebResponse response = evt.GetResponse();
    const wxString body = response.AsString();
    Unbind(wxEVT_WEBREQUEST_STATE, &UpdateChecker::OnRequestState, this);

    nlohmann::json root = nlohmann::json::parse(
        jsonutil::ToUtf8(body), /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
      KLOG_WARN() << "Update check: malformed JSON from " << kReleasesUrl;
      if (m_onError)
        m_onError(_("The update server returned malformed data"));
      return;
    }

    auto platformIt = root.find(PlatformKey());
    if (platformIt == root.end() || !platformIt->is_object()) {
      KLOG_WARN() << "Update check: no entry for platform '" << PlatformKey()
                  << "'";
      if (m_onError)
        m_onError(_("No release information for this platform"));
      return;
    }

    UpdateCheckResult result;
    result.latestVersion = jsonutil::GetStr(*platformIt, "version");
    result.downloadUrl = jsonutil::GetStr(*platformIt, "download_url");
    result.updateAvailable =
        IsVersionNewer(result.latestVersion, m_currentVersion);

    KLOG_INFO() << "Update check: current=" << m_currentVersion
                << " latest=" << result.latestVersion
                << " updateAvailable=" << result.updateAvailable;

    if (m_onResult)
      m_onResult(result);
    return;
  }
  case wxWebRequest::State_Failed:
  case wxWebRequest::State_Unauthorized:
  case wxWebRequest::State_Cancelled: {
    Unbind(wxEVT_WEBREQUEST_STATE, &UpdateChecker::OnRequestState, this);
    const wxString error = evt.GetErrorDescription();
    KLOG_WARN() << "Update check failed: " << error;
    if (m_onError)
      m_onError(error);
    return;
  }
  default:
    // State_Idle / State_Active: request still in flight.
    return;
  }
}
