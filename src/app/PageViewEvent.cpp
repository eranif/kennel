#include "app/PageViewEvent.hpp"

#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/window.h>

wxDEFINE_EVENT(wxEVT_PAGEVIEW_SELECTED, PageViewEvent);
wxDEFINE_EVENT(wxEVT_PAGEVIEW_MENU, PageViewEvent);

void StylePageView(wxDataViewCtrl *ctrl) {
  ctrl->SetAlternateRowColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW).ChangeLightness(105));
}

void FillPanel(wxWindow *host, wxWindow *child) {
  wxSizer *sizer = host->GetSizer();
  if (sizer == nullptr) {
    sizer = new wxBoxSizer(wxVERTICAL);
    host->SetSizer(sizer);
  }
  sizer->Add(child, wxSizerFlags(1).Expand());
  host->Layout();
}
