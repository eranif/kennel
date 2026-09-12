#pragma once

#include "app/UI.hpp"
#include <wx/dialog.h>

class wxNotebook;
class wxPanel;
class wxTextCtrl;
class wxStaticText;

class AboutDialog : public AboutDialogBase {
public:
  explicit AboutDialog(wxWindow *parent);
  ~AboutDialog() override;

  void SetAppInfo(const wxString &appName, const wxString &version,
                  const wxString &description, const wxString &author,
                  const wxString &website);
  void SetLicenseText(const wxString &license);
};
