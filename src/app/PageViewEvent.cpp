#include "app/PageViewEvent.hpp"

#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/window.h>

wxDEFINE_EVENT(wxEVT_PAGEVIEW_SELECTED, PageViewEvent);
wxDEFINE_EVENT(wxEVT_PAGEVIEW_MENU, PageViewEvent);

namespace {
// A little different from the background of the control. Lightening white
// does nothing, so a light background gets darker and a dark one lighter.
wxColour AlternateRowColour() {
  const wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
  const int brightness =
      (bg.Red() * 299 + bg.Green() * 587 + bg.Blue() * 114) / 1000;
  return bg.ChangeLightness(brightness >= 128 ? 95 : 108);
}
} // namespace

void StylePageView(wxDataViewCtrl *ctrl) {
  ctrl->SetAlternateRowColour(AlternateRowColour());
  // The system colours change with the theme of the application.
  ctrl->Bind(wxEVT_SYS_COLOUR_CHANGED, [ctrl](wxSysColourChangedEvent &event) {
    event.Skip();
    ctrl->SetAlternateRowColour(AlternateRowColour());
  });
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
