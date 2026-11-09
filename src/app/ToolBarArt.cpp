#include "app/ToolBarArt.h"

#include <wx/aui/framemanager.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <wx/settings.h>

#include <memory>

namespace {
// In DIPs.
// Added to the size of a tool: across the bar (the width of a vertical bar, the
// height of a horizontal one) and along it.
constexpr int kCrossPadding = 18;
constexpr int kAlongPadding = 10;
constexpr int kPillInset = 4;     // Between the pill and the edge of the bar
constexpr int kHighlightGap = 2;  // Between the highlight and the pill
constexpr int kSeparatorSize = 8; // The gap between two pills

bool IsDark() { return wxSystemSettings::GetAppearance().IsDark(); }

// The colour of the bar: the same as the bar of the tabs.
wxColour BarColour() {
  return wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
}

// A pill looks sunk into the bar: its fill is darker than the bar, and its
// outline is darker than its fill.
wxColour PillColour() {
  return BarColour().ChangeLightness(IsDark() ? 104 : 94);
}
wxColour PillOutlineColour() { return BarColour().ChangeLightness(110); }

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
  // A gradient of the bar colour across the bar: from the left to the right in
  // a vertical bar, from the top to the bottom in a horizontal one. It starts
  // a little lighter and ends a little darker.
  const bool vertical = (m_flags & wxAUI_TB_VERTICAL) != 0;
  dc.GradientFillLinear(rect, Shade(bar, 112, 103), Shade(bar, 90, 88),
                        vertical ? wxEAST : wxSOUTH);
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
  }
  return Style::Soft;
}

void KennelToolBarArt::DrawHighlight(wxDC &dc, wxWindow *wnd, const wxRect &box,
                                     bool hover, bool pressed,
                                     bool checked) const {
  // The highlights are on a pill.
  const wxColour bar = PillColour();
  const wxColour accent = Accent();
  // Every style draws its highlight as a circle in the middle of the tool.
  const int side = std::min(box.width, box.height);
  const wxRect circle(box.x + (box.width - side) / 2,
                      box.y + (box.height - side) / 2, side, side);
  const double radius = side / 2.0;
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
    DrawRounded(dc, circle, radius, fill);
    break;
  }
  case Style::Accent: {
    // The tool is the accent colour; pressed is the strongest.
    const int alpha = pressed ? 150 : checked ? 115 : 75;
    DrawRounded(dc, circle, radius, Blend(bar, accent, alpha));
    break;
  }
  case Style::Outline: {
    const wxColour ring =
        pressed ? accent : WithAlpha(accent, hover ? 230 : 170);
    const wxColour fill = checked
                              ? Blend(bar, accent, pressed ? 80 : 45)
                              : (pressed ? Shade(bar, 125, 92) : wxColour());
    DrawRounded(dc, circle, radius, fill, ring, ringWidth);
    break;
  }
  case Style::Indicator: {
    // A tint; a checked tool has the accent bar on the outer edge of the tool
    // (the left of a vertical bar, the top of a horizontal one).
    wxColour fill = pressed ? Shade(bar, 145, 82) : Shade(bar, 125, 92);
    if (checked && !pressed) {
      fill = Blend(bar, accent, hover ? 55 : 35);
    }
    DrawRounded(dc, circle, radius, fill);
    if (checked) {
      const int thickness = std::max(2, wnd->FromDIP(3));
      dc.SetPen(*wxTRANSPARENT_PEN);
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
    // A circle on a sunk pill is raised: lighter than the pill.
    const wxColour face = BarColour();
    // Dark theme: the hover is the colour that the pressed state had, and the
    // pressed state is 10 darker than the hover.
    wxColour fill = pressed ? Shade(face, 102, 99) : Shade(face, 112, 106);
    wxColour ring;
    if (checked) {
      fill = Blend(bar, accent, pressed ? 90 : 50);
      ring = accent;
    }
    DrawRounded(dc, circle, radius, fill, ring, ringWidth);
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

  DrawGroupPill(dc, wnd, item);

  const int state = item.GetState();
  if ((state & wxAUI_BUTTON_STATE_DISABLED) == 0) {
    const bool pressed = (state & wxAUI_BUTTON_STATE_PRESSED) != 0;
    const bool hover = (state & wxAUI_BUTTON_STATE_HOVER) != 0;
    const bool checked =
        (state & wxAUI_BUTTON_STATE_CHECKED) != 0 || item.IsSticky();
    if (pressed || hover || checked) {
      // Inside the pill: more room on the sides of the pill than along it.
      const int cross = wnd->FromDIP(kPillInset + kHighlightGap);
      const int along = wnd->FromDIP(kHighlightGap);
      wxRect box = rect;
      if (m_flags & wxAUI_TB_VERTICAL) {
        box.Deflate(cross, along);
      } else {
        box.Deflate(along, cross);
      }
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

void KennelToolBarArt::DrawSeparator(wxDC &WXUNUSED(dc),
                                     wxWindow *WXUNUSED(wnd),
                                     const wxRect &WXUNUSED(rect)) {
  // The gap between two pills is the separator.
}

void KennelToolBarArt::DrawGripper(wxDC &WXUNUSED(dc), wxWindow *WXUNUSED(wnd),
                                   const wxRect &WXUNUSED(rect)) {
  // The bar is fixed: no gripper.
}

wxSize KennelToolBarArt::GetToolSize(wxReadOnlyDC &dc, wxWindow *wnd,
                                     const wxAuiToolBarItem &item) {
  wxSize size = wxAuiGenericToolBarArt::GetToolSize(dc, wnd, item);
  const int cross = wnd->FromDIP(kCrossPadding);
  const int along = wnd->FromDIP(kAlongPadding);
  if (m_flags & wxAUI_TB_VERTICAL) {
    size.IncBy(cross, along);
  } else {
    size.IncBy(along, cross);
  }
  return size;
}

KennelToolBarArt::KennelToolBarArt(Style style) : m_style(style) {
  m_separatorSize = kSeparatorSize;
}

namespace {
bool IsButton(const wxAuiToolBarItem *tool) {
  if (tool == nullptr || tool->GetSizerItem() == nullptr ||
      !tool->GetSizerItem()->IsShown()) {
    return false;
  }
  const int kind = tool->GetKind();
  return kind == wxITEM_NORMAL || kind == wxITEM_CHECK || kind == wxITEM_RADIO;
}
} // namespace

void KennelToolBarArt::DrawGroupPill(wxDC &dc, wxWindow *wnd,
                                     const wxAuiToolBarItem &item) {
  auto *bar = dynamic_cast<wxAuiToolBar *>(wnd);
  if (bar == nullptr) {
    return;
  }
  const int count = static_cast<int>(bar->GetToolCount());
  int first = wxNOT_FOUND;
  for (int i = 0; i < count; ++i) {
    if (bar->FindToolByIndex(i) == &item) {
      first = i;
      break;
    }
  }
  // Only the first button of a group draws the pill.
  if (first == wxNOT_FOUND ||
      (first > 0 && IsButton(bar->FindToolByIndex(first - 1)))) {
    return;
  }

  // The pill covers the buttons up to the next separator, or up to the end of
  // the place that the bar has (the others are in the overflow menu).
  const wxSize client = bar->GetClientSize();
  wxRect group;
  for (int i = first; i < count; ++i) {
    const wxAuiToolBarItem *tool = bar->FindToolByIndex(i);
    if (!IsButton(tool)) {
      break;
    }
    const wxRect rect = tool->GetSizerItem()->GetRect();
    if (rect.GetRight() >= client.x || rect.GetBottom() >= client.y) {
      break;
    }
    group = group.IsEmpty() ? rect : group.Union(rect);
  }
  if (group.IsEmpty()) {
    return;
  }

  // Room at the sides of the bar. The pill is round at its ends.
  const int inset = wnd->FromDIP(kPillInset);
  if (m_flags & wxAUI_TB_VERTICAL) {
    group.Deflate(inset, 0);
  } else {
    group.Deflate(0, inset);
  }
  const double ringWidth = std::max(1, wnd->FromDIP(1));
  DrawRounded(dc, group, std::min(group.width, group.height) / 2.0,
              PillColour(), PillOutlineColour(), ringWidth);
}
