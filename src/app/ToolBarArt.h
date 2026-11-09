#pragma once

#include <wx/aui/auibar.h>

// The look of the tools of a wxAuiToolBar in Kennel. It is made to match the
// tab bars (GlyphBook): the bar has the colour of the window (the button face),
// a tool gets a highlight on hover and while pressed, and a tool that is
// checked is marked with the accent colour of the system. There are no
// borders and no gripper, the bar is a soft gradient across its width, and a
// thin line separates it from the view next to it. The Style says how a tool is
// highlighted; the highlight is always a circle.
//
// Like the toolbars of macOS, the buttons between two separators are a group,
// and each group is on a rounded "pill" that looks sunk into the bar: its fill
// is darker than the bar, and its outline is darker than its fill.
// A separator is only a gap between two pills. The pill of a group is drawn
// with its first button (the bar background is a bitmap that wxAuiToolBar
// keeps, made before the tools have their places).
//
// All the colours come from the system colours and the appearance (dark or
// light) when something is drawn, so a toolbar follows a change of the theme
// without a new art. Use it with SetArtProvider(); the toolbar takes the art.
class KennelToolBarArt : public wxAuiGenericToolBarArt {
public:
  enum class Style {
    Soft,      // A tint of the bar colour (lighter or darker)
    Accent,    // A fill with the accent colour, stronger when pressed
    Outline,   // A ring of the accent colour, with a soft fill when checked
    Indicator, // A tint, and an accent bar on the outer edge when checked
    Circle,    // A round tint, with a ring of the accent colour when checked
  };

  explicit KennelToolBarArt(Style style = Style::Circle);

  // The style called `name` (soft, accent, outline, indicator, circle;
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
  // Draws the pill of the group of `item`, when `item` is the first button of
  // its group.
  void DrawGroupPill(wxDC &dc, wxWindow *wnd, const wxAuiToolBarItem &item);
  // The highlight of a tool that has the mouse over it, is pressed or is
  // checked, in `box`.
  void DrawHighlight(wxDC &dc, wxWindow *wnd, const wxRect &box, bool hover,
                     bool pressed, bool checked) const;

  Style m_style;
};
