#pragma once

#include <wx/aui/auibar.h>

// The look of the tools of a wxAuiToolBar in Kennel. It is made to match the
// tab bars (GlyphBook): the bar has the colour of the window (the button face),
// a tool gets a highlight on hover and while pressed, and a tool that is
// checked is marked with the accent colour of the system. There are no
// borders, no gradients and no gripper, and a thin line separates the bar from
// the view next to it. The Style says how a tool is highlighted.
//
// All the colours come from the system colours and the appearance (dark or
// light) when something is drawn, so a toolbar follows a change of the theme
// without a new art. Use it with SetArtProvider(); the toolbar takes the art.
class KennelToolBarArt : public wxAuiGenericToolBarArt {
public:
  enum class Style {
    Soft,      // A rounded tint of the bar colour (lighter or darker)
    Accent,    // A rounded fill with the accent colour, stronger when pressed
    Outline,   // A ring of the accent colour, with a soft fill when checked
    Indicator, // A square tint, and an accent bar on the outer edge when
               // checked
    Circle,    // A round tint, with a ring of the accent colour when checked
    Glass,     // A see-through white (dark theme) or black (light theme) veil
  };

  explicit KennelToolBarArt(Style style = Style::Soft) : m_style(style) {}

  // The style called `name` (soft, accent, outline, indicator, circle, glass;
  // the case does not matter), or Soft when there is no such style.
  static Style StyleFromName(const wxString &name);

  wxAuiToolBarArt *Clone() override { return new KennelToolBarArt(*this); }

  void DrawBackground(wxDC &dc, wxWindow *wnd, const wxRect &rect) override;
  void DrawPlainBackground(wxDC &dc, wxWindow *wnd,
                           const wxRect &rect) override;
  void DrawButton(wxDC &dc, wxWindow *wnd, const wxAuiToolBarItem &item,
                  const wxRect &rect) override;
  void DrawSeparator(wxDC &dc, wxWindow *wnd, const wxRect &rect) override;
  void DrawGripper(wxDC &dc, wxWindow *wnd, const wxRect &rect) override;
  // A tool is a little bigger than its image: the highlight needs room around
  // it.
  wxSize GetToolSize(wxReadOnlyDC &dc, wxWindow *wnd,
                     const wxAuiToolBarItem &item) override;

private:
  // The highlight of a tool that has the mouse over it, is pressed or is
  // checked, in `box`.
  void DrawHighlight(wxDC &dc, wxWindow *wnd, const wxRect &box, bool hover,
                     bool pressed, bool checked) const;

  Style m_style;
};
