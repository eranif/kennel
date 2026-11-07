#pragma once

#include <wx/dataview.h>
#include <wx/settings.h>

namespace dv {
inline void AutoSizeColumns(wxDataViewCtrl *ctrl) {
  for (unsigned int i = 0; i < ctrl->GetColumnCount(); ++i) {
    ctrl->GetColumn(i)->SetWidth(wxCOL_WIDTH_AUTOSIZE);
  }
}

inline void StylePageView(wxDataViewCtrl *ctrl) {
  if (ctrl->GetWindowStyleFlag() & wxDV_ROW_LINES) {
    const wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    const bool isDark = wxSystemSettings::GetAppearance().IsDark();
    const auto alternateColour = bg.ChangeLightness(isDark ? 105 : 95);
    ctrl->SetAlternateRowColour(alternateColour);
    // The system colours change with the theme of the application.
    ctrl->Bind(wxEVT_SYS_COLOUR_CHANGED,
               [ctrl, alternateColour](wxSysColourChangedEvent &event) {
                 event.Skip();
                 ctrl->SetAlternateRowColour(alternateColour);
               });
  }
}
} // namespace dv
