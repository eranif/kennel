#include "AboutDialog.hpp"

#include "app/AssetBootstrap.h"
#include "core/Config.h"
#include "core/Helpers.h"
#include "core/Version.h"

#include <fstream>
#include <sstream>
#include <wx/button.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

AboutDialog::AboutDialog(wxWindow *parent) : AboutDialogBase(parent) {
  m_textCtrlName->SetEditable(false);
  m_textCtrlVersion->SetEditable(false);
  m_textCtrlAuthor->SetEditable(false);
  m_textCtrlDescription->SetEditable(false);
  const wxString appIcon = ResolveIconPath("kennel.svg");
  const wxBitmapBundle bb =
      wxBitmapBundle::FromSVGFile(appIcon, wxSize(128, 128));
  if (bb.IsOk()) {
    m_staticBitmapIcon->SetBitmap(bb);
  }

  wxFont f = wxSystemSettings::GetFont(wxSYS_ANSI_FIXED_FONT);
  f.SetFaceName(GetDefaultFontFamily());
  f.SetPointSize(GetDefaultFontSize());
  for (int i = 0; i < wxSTC_STYLE_MAX; ++i) {
    m_stcLicense->StyleSetFont(i, f);
  }

  SetAppInfo(kAppName, kAppVersion,
             _("A terminal session manager for managing AI agents."),
             "Eran Ifrah", "https://github.com/eranif/kennel");

  wxString licensePath = GetLicensePath();
  if (!licensePath.empty()) {
    std::ifstream file(licensePath.ToStdString(wxConvUTF8));
    if (file.is_open()) {
      std::stringstream buffer;
      buffer << file.rdbuf();
      SetLicenseText(wxString::FromUTF8(buffer.str()));
    }
  }

  GetSizer()->Fit(this);
  Layout();
  PostSizeEvent();
  ::PositionDialog(this, Orientation::kTop);
}

AboutDialog::~AboutDialog() {}

void AboutDialog::SetAppInfo(const wxString &appName, const wxString &version,
                             const wxString &description,
                             const wxString &author, const wxString &website) {
  m_textCtrlName->SetValue(appName);
  m_textCtrlVersion->SetValue(version);
  m_textCtrlAuthor->SetValue(author);
  m_textCtrlDescription->SetValue(description);
}

void AboutDialog::SetLicenseText(const wxString &license) {
  m_stcLicense->SetValue(license);
  m_stcLicense->SetReadOnly(true);
}
