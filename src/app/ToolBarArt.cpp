#include "app/ToolBarArt.h"

#include <wx/aui/framemanager.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <wx/settings.h>

#include <memory>

namespace {
// In DIPs.
constexpr int kToolPadding = 6;  // Added to the size of a tool
constexpr int kHighlightGap = 2; // Between the highlight and the tool border
constexpr int kHighlightRadius = 6;

bool IsDark() { return wxSystemSettings::GetAppearance().IsDark(); }

// The colour of the bar: the same as the bar of the tabs.
wxColour BarColour() {
  return wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
}

// `bar` made lighter on a dark theme and darker on a light one.
wxColour Shade(const wxColour &bar, int darkLightness, int lightLightness) {
  return bar.ChangeLightness(IsDark() ? darkLightness : lightLightness);
}

// `over` on `under`, `alpha` of 0 to 255 is how much of `over` there is.
wxColour Blend(const wxColour &under, const wxColour &over, int alpha) {
  const auto mix = [alpha](unsigned char below, unsigned char above) {
    return static_cast<unsigned char>((above * alpha + below * (255 - alpha)) /
                                      255);
  };
  return wxColour(mix(under.Red(), over.Red()),
                  mix(under.Green(), over.Green()),
                  mix(under.Blue(), over.Blue()));
}

// A rounded rectangle with smooth edges when the platform can draw them. The
// colours can have alpha (when the platform can draw it). `ring` is the colour
// of the outline, or not ok for none.
void DrawRounded(wxDC &dc, const wxRect &rect, double radius,
                 const wxColour &fill, const wxColour &ring = wxColour(),
                 double ringWidth = 1.0) {
  std::unique_ptr<wxGraphicsContext> gc{
      wxGraphicsContext::CreateFromUnknownDC(dc)};
  if (gc) {
    gc->SetPen(ring.IsOk()
                   ? gc->CreatePen(wxGraphicsPenInfo(ring).Width(ringWidth))
                   : wxNullGraphicsPen);
    gc->SetBrush(fill.IsOk() ? gc->CreateBrush(wxBrush(fill))
                             : wxNullGraphicsBrush);
    // The line is centred on the edge: keep it inside the rectangle.
    const double half = ring.IsOk() ? ringWidth / 2.0 : 0.0;
    gc->DrawRoundedRectangle(rect.x + half, rect.y + half,
                             rect.width - ringWidth * (ring.IsOk() ? 1 : 0),
                             rect.height - ringWidth * (ring.IsOk() ? 1 : 0),
                             radius);
    return;
  }
  dc.SetPen(ring.IsOk() ? wxPen(ring, static_cast<int>(ringWidth))
                        : *wxTRANSPARENT_PEN);
  dc.SetBrush(fill.IsOk() ? wxBrush(fill) : *wxTRANSPARENT_BRUSH);
  dc.DrawRoundedRectangle(rect, radius);
}

wxColour Accent() {
  return wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
}

// `colour` with an alpha of 0 to 255.
wxColour WithAlpha(const wxColour &colour, int alpha) {
  return wxColour(colour.Red(), colour.Green(), colour.Blue(),
                  static_cast<unsigned char>(alpha));
}
} // namespace

void KennelToolBarArt::DrawBackground(wxDC &dc, wxWindow *wnd,
                                      const wxRect &rect) {
  const wxColour bar = BarColour();
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(bar));
  dc.DrawRectangle(rect);

  // A thin line along the side next to the view: the right side of a vertical
  // bar, the bottom of a horizontal one.
  const int thickness = std::max(1, wnd->FromDIP(1));
  dc.SetBrush(wxBrush(Shade(bar, 130, 88)));
  if (m_flags & wxAUI_TB_VERTICAL) {
    dc.DrawRectangle(rect.x + rect.width - thickness, rect.y, thickness,
                     rect.height);
  } else {
    dc.DrawRectangle(rect.x, rect.y + rect.height - thickness, rect.width,
                     thickness);
  }
}

void KennelToolBarArt::DrawPlainBackground(wxDC &dc, wxWindow *wnd,
                                           const wxRect &rect) {
  DrawBackground(dc, wnd, rect);
}

KennelToolBarArt::Style KennelToolBarArt::StyleFromName(const wxString &name) {
  const wxString lower = name.Lower();
  if (lower == "accent") {
    return Style::Accent;
  } else if (lower == "outline") {
    return Style::Outline;
  } else if (lower == "indicator") {
    return Style::Indicator;
  } else if (lower == "circle") {
    return Style::Circle;
  } else if (lower == "glass") {
    return Style::Glass;
  }
  return Style::Soft;
}

void KennelToolBarArt::DrawHighlight(wxDC &dc, wxWindow *wnd, const wxRect &box,
                                     bool hover, bool pressed,
                                     bool checked) const {
  const wxColour bar = BarColour();
  const wxColour accent = Accent();
  const double radius = wnd->FromDIP(kHighlightRadius);
  const double ringWidth = std::max(1.0, wnd->FromDIP(1) * 1.5);

  switch (m_style) {
  case Style::Soft: {
    wxColour fill;
    if (pressed) {
      fill = checked ? Blend(bar, accent, 110) : Shade(bar, 145, 82);
    } else if (checked) {
      fill = Blend(bar, accent, hover ? 85 : 60);
    } else {
      fill = Shade(bar, 125, 92);
    }
    DrawRounded(dc, box, radius, fill);
    break;
  }
  case Style::Accent: {
    // The tool is the accent colour; pressed is the strongest.
    const int alpha = pressed ? 150 : checked ? 115 : 75;
    DrawRounded(dc, box, radius, Blend(bar, accent, alpha));
    break;
  }
  case Style::Outline: {
    const wxColour ring =
        pressed ? accent : WithAlpha(accent, hover ? 230 : 170);
    const wxColour fill = checked
                              ? Blend(bar, accent, pressed ? 80 : 45)
                              : (pressed ? Shade(bar, 125, 92) : wxColour());
    DrawRounded(dc, box, radius, fill, ring, ringWidth);
    break;
  }
  case Style::Indicator: {
    // A square tint over the whole tool; a checked tool has the accent bar on
    // the outer edge (the left of a vertical bar, the top of a horizontal one).
    wxColour fill = pressed ? Shade(bar, 145, 82) : Shade(bar, 125, 92);
    if (checked && !pressed) {
      fill = Blend(bar, accent, hover ? 55 : 35);
    }
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(fill));
    dc.DrawRectangle(box);
    if (checked) {
      const int thickness = std::max(2, wnd->FromDIP(3));
      dc.SetBrush(wxBrush(accent));
      if (m_flags & wxAUI_TB_VERTICAL) {
        dc.DrawRectangle(box.x, box.y, thickness, box.height);
      } else {
        dc.DrawRectangle(box.x, box.y, box.width, thickness);
      }
    }
    break;
  }
  case Style::Circle: {
    const int side = std::min(box.width, box.height);
    const wxRect circle(box.x + (box.width - side) / 2,
                        box.y + (box.height - side) / 2, side, side);
    wxColour fill = pressed ? Shade(bar, 145, 82) : Shade(bar, 125, 92);
    wxColour ring;
    if (checked) {
      fill = Blend(bar, accent, pressed ? 90 : 50);
      ring = accent;
    }
    DrawRounded(dc, circle, side / 2.0, fill, ring, ringWidth);
    break;
  }
  case Style::Glass: {
    // A veil: white on a dark theme, black on a light one. The tint of the
    // bar shows through, so it fits any bar colour.
    const wxColour veil = IsDark() ? *wxWHITE : *wxBLACK;
    const int alpha = pressed ? 55 : hover ? 32 : 0;
    if (checked) {
      DrawRounded(dc, box, radius, WithAlpha(accent, pressed ? 120 : 85),
                  WithAlpha(accent, 200), ringWidth);
    } else {
      DrawRounded(dc, box, radius, WithAlpha(veil, alpha));
    }
    break;
  }
  }
}

void KennelToolBarArt::DrawButton(wxDC &dc, wxWindow *wnd,
                                  const wxAuiToolBarItem &item,
                                  const wxRect &rect) {
  // A tool with a label is rare here: the generic art does the layout of the
  // text.
  if (m_flags & wxAUI_TB_TEXT) {
    wxAuiGenericToolBarArt::DrawButton(dc, wnd, item, rect);
    return;
  }

  const int state = item.GetState();
  if ((state & wxAUI_BUTTON_STATE_DISABLED) == 0) {
    const bool pressed = (state & wxAUI_BUTTON_STATE_PRESSED) != 0;
    const bool hover = (state & wxAUI_BUTTON_STATE_HOVER) != 0;
    const bool checked =
        (state & wxAUI_BUTTON_STATE_CHECKED) != 0 || item.IsSticky();
    if (pressed || hover || checked) {
      wxRect box = rect;
      box.Deflate(wnd->FromDIP(kHighlightGap));
      DrawHighlight(dc, wnd, box, hover, pressed, checked);
    }
  }

  const wxBitmap bmp = item.GetCurrentBitmapFor(wnd);
  if (bmp.IsOk()) {
    const wxSize size = bmp.GetLogicalSize();
    dc.DrawBitmap(bmp, rect.x + (rect.width - size.x) / 2,
                  rect.y + (rect.height - size.y) / 2, true);
  }
}

void KennelToolBarArt::DrawSeparator(wxDC &dc, wxWindow *wnd,
                                     const wxRect &rect) {
  // A short line in the middle of the space, across the direction of the bar.
  const int thickness = std::max(1, wnd->FromDIP(1));
  wxRect line = rect;
  if (m_flags & wxAUI_TB_VERTICAL) {
    line.y += (rect.height - thickness) / 2;
    line.height = thickness;
    line.x += rect.width / 4;
    line.width = rect.width / 2;
  } else {
    line.x += (rect.width - thickness) / 2;
    line.width = thickness;
    line.y += rect.height / 4;
    line.height = rect.height / 2;
  }
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(Shade(BarColour(), 130, 88)));
  dc.DrawRectangle(line);
}

void KennelToolBarArt::DrawGripper(wxDC &WXUNUSED(dc), wxWindow *WXUNUSED(wnd),
                                   const wxRect &WXUNUSED(rect)) {
  // The bar is fixed: no gripper.
}

wxSize KennelToolBarArt::GetToolSize(wxReadOnlyDC &dc, wxWindow *wnd,
                                     const wxAuiToolBarItem &item) {
  wxSize size = wxAuiGenericToolBarArt::GetToolSize(dc, wnd, item);
  const int padding = wnd->FromDIP(kToolPadding);
  size.IncBy(padding, padding);
  return size;
}
