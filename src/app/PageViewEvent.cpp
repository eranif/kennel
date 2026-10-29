#include "app/PageViewEvent.hpp"

#include <wx/settings.h>
#include <wx/window.h>

wxDEFINE_EVENT(wxEVT_SESSION_PAGE_ADDED, PageViewEvent);
wxDEFINE_EVENT(wxEVT_SESSION_PAGE_REMOVED, PageViewEvent);
wxDEFINE_EVENT(wxEVT_SESSION_PAGE_RENAMED, PageViewEvent);
wxDEFINE_EVENT(wxEVT_SESSION_PAGES_CLEARED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SESSION_PAGE_SHOWN, PageViewEvent);
wxDEFINE_EVENT(wxEVT_PAGES_TOOLBAR_STYLE, wxCommandEvent);

namespace {
// A little different from the background of the control. Lightening white
// does nothing, so a light background gets darker and a dark one lighter.
wxColour AlternateRowColour() {
  const wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
  const bool isDark = wxSystemSettings::GetAppearance().IsDark();
  return bg.ChangeLightness(isDark ? 105 : 95);
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
