#include "app/wxCustomStatusBar.h"

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/settings.h>

namespace {
constexpr int kSpace =
    5; // Around the content of a field, and between its parts

bool IsLowSurrogate(const wxUniChar &ch) {
  return ch.GetValue() >= 0xDC00 && ch.GetValue() <= 0xDFFF;
}

// Fits `text` into `maxWidth` pixels, ending it with "..." when it is cut.
wxString TruncateText(const wxString &text, int maxWidth, wxDC &dc) {
  if (maxWidth <= 0) {
    return wxEmptyString;
  }
  if (dc.GetTextExtent(text).GetWidth() <= maxWidth) {
    return text;
  }
  static const wxString kEllipsis = wxT("...");
  // The longest start of `text` that fits with the ellipsis (a longer start is
  // never narrower, so a binary search finds it).
  const auto fits = [&](size_t len) {
    return dc.GetTextExtent(text.Left(len) + kEllipsis).GetWidth() <= maxWidth;
  };
  if (!fits(0)) {
    return wxEmptyString;
  }
  size_t low = 0;              // fits
  size_t high = text.length(); // does not fit (the whole text is too wide)
  while (high - low > 1) {
    const size_t mid = low + (high - low) / 2;
    (fits(mid) ? low : high) = mid;
  }
  // Do not cut a surrogate pair (UTF-16 builds) in the middle.
  while (low > 0 && IsLowSurrogate(text[low])) {
    --low;
  }
  return text.Left(low) + kEllipsis;
}
} // namespace

//========================------------------------------------
// wxCustomStatusBarArt
//========================------------------------------------

void wxCustomStatusBarArt::DrawText(wxDC &dc, wxCoord x, wxCoord y,
                                    const wxString &text) {
  dc.SetTextForeground(GetTextColour());
  dc.DrawText(text, x, y);
}

void wxCustomStatusBarArt::DrawFieldSeparator(wxDC &dc,
                                              const wxRect &fieldRect) {
  // draw border line
  dc.SetPen(GetPenColour());
  wxPoint bottomPt, topPt;

  topPt = fieldRect.GetTopLeft();
  topPt.y += 2;

  bottomPt = fieldRect.GetBottomLeft();
  bottomPt.y += 1;
  dc.DrawLine(topPt, bottomPt);
}

wxColour wxCustomStatusBarArt::GetBgColour() const {
  return wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
}
wxColour wxCustomStatusBarArt::GetPenColour() const { return GetBgColour(); }
wxColour wxCustomStatusBarArt::GetTextColour() const {
  return wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
}

//========================------------------------------------
// Fields
//========================------------------------------------

bool wxCustomStatusBarField::HitTest(const wxPoint &point) const {
  return m_rect.Contains(point);
}

void wxCustomStatusBarFieldText::Render(wxDC &dc, const wxRect &rect,
                                        wxCustomStatusBarArt::Ptr_t art) {
  m_rect = rect;
  // Leave some space on each side; cut the text with "..." if it does not fit.
  const wxString text = TruncateText(m_text, rect.GetWidth() - 2 * kSpace, dc);
  wxSize textSize = dc.GetTextExtent(text);

  // Center text
  wxCoord textY = (rect.GetHeight() - textSize.GetHeight()) / 2 + rect.y;
  wxCoord textX;
  if (m_textAlign == wxALIGN_CENTER) {
    textX = (rect.GetWidth() - textSize.GetWidth()) / 2 + rect.x;
  } else {
    // left
    textX = rect.x + kSpace;
  }

  // draw border line
  art->DrawFieldSeparator(dc, rect);

  // Draw the text
  art->DrawText(dc, textX, textY + 1, text);
}

void wxCustomStatusBarFieldText::SetText(const wxString &text) {
  if (m_text == text) {
    return; // Nothing to repaint
  }
  m_text = text;
  if (m_parent != nullptr) {
    m_parent->Refresh();
  }
}

size_t wxCustomStatusBarBitmapField::GetBestWidth(int labelWidth) const {
  int width = 2 * kSpace + labelWidth;
  if (m_bitmap.IsOk()) {
    width += m_bitmap.GetScaledSize().GetWidth() + kSpace;
  }
  return width;
}

void wxCustomStatusBarBitmapField::Render(wxDC &dc, const wxRect &rect,
                                          wxCustomStatusBarArt::Ptr_t art) {
  m_rect = rect;

  // draw border line
  art->DrawFieldSeparator(dc, rect);

  int xx = rect.x + kSpace;
  int remaining_width = rect.GetWidth() - 2 * kSpace;
  if (m_bitmap.IsOk()) {
    const wxSize size = m_bitmap.GetScaledSize();
    wxRect rr{wxPoint(xx, rect.y), size};
    rr = rr.CenterIn(rect, wxVERTICAL);
    dc.DrawBitmap(m_bitmap, rr.GetTopLeft());
    // The space after the icon is only there when a label follows
    xx += size.GetWidth() + kSpace;
    remaining_width -= size.GetWidth() + kSpace;
  }

  if (!m_label.empty()) {
    const wxString fixed_text = TruncateText(m_label, remaining_width, dc);
    wxRect text_rect = dc.GetTextExtent(fixed_text);
    text_rect = text_rect.CenterIn(rect, wxVERTICAL);
    text_rect.SetX(xx);
    dc.SetTextForeground(art->GetTextColour());
    dc.DrawText(fixed_text, text_rect.GetTopLeft());
  }
}

//========================------------------------------------
// wxCustomStatusBar
//========================------------------------------------

wxCustomStatusBar::wxCustomStatusBar(wxWindow *parent, wxWindowID id,
                                     long style)
    : wxStatusBar(parent, id, style), m_art(new wxCustomStatusBarArt),
      m_mainText(new wxCustomStatusBarFieldText(this, 0)) {
  m_mainText->SetAutoWidthEnabled(true);
  m_mainText->SetTextAlignment(wxALIGN_LEFT);

  SetBackgroundStyle(wxBG_STYLE_PAINT);

  Bind(wxEVT_PAINT, &wxCustomStatusBar::OnPaint, this);
  Bind(wxEVT_ERASE_BACKGROUND, &wxCustomStatusBar::OnEraseBackground, this);
  Bind(wxEVT_MOTION, &wxCustomStatusBar::OnMouseMotion, this);

  m_fields.push_back(m_mainText);
}

void wxCustomStatusBar::OnPaint(wxPaintEvent &event) {
  wxUnusedVar(event);
  wxAutoBufferedPaintDC dc(this);
  PrepareDC(dc);
  wxRect rect = GetClientRect();
  rect.Inflate(1);

  dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

  // Fill the background
  wxColour bgColour = m_art->GetBgColour();
  dc.SetBrush(bgColour);
  dc.SetPen(bgColour);
  dc.DrawRectangle(rect);

  // Update the fields width
  Finalize();

  //===----------------------
  // Draw the fields
  //===----------------------
  size_t offset_x = 0;
  for (const auto &field : m_fields) {
    // Prepare the rect
    size_t width =
        field->IsAutoWidth() ? field->GetAutoWidth() : field->GetWidth();
    if (width == 0) {
      // Nothing to draw (no separator either); it must not be hit either.
      field->SetRect(wxRect());
      continue;
    }
    wxRect fieldRect(offset_x, rect.y, width, rect.height);
    dc.SetClippingRegion(fieldRect);
    field->Render(dc, fieldRect, m_art);
    dc.DestroyClippingRegion();
    offset_x += width;
  }
}

int wxCustomStatusBar::GetTextWidth(const wxString &text) const {
  // The font OnPaint() draws with, not the window's own (which may differ).
  wxClientDC dc(const_cast<wxCustomStatusBar *>(this));
  dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
  return dc.GetTextExtent(text).GetWidth();
}

void wxCustomStatusBar::OnEraseBackground(wxEraseEvent &event) {
  wxUnusedVar(event);
}

void wxCustomStatusBar::UpdateMainTextField() {
  m_mainText->SetRect(DoGetMainFieldRect());
  m_mainText->SetText(m_text);
  m_mainText->SetTooltip(m_text);
}

void wxCustomStatusBar::Finalize() {
  std::vector<wxCustomStatusBarField *> dyn_width_fields;
  size_t fixed_field_width = 0;
  for (const auto &field : m_fields) {
    if (field->IsAutoWidth()) {
      dyn_width_fields.push_back(field.get());
    } else {
      fixed_field_width += field->GetWidth();
    }
  }

  if (dyn_width_fields.empty())
    return; // nothing to be done here

  // The fixed fields may be wider than the bar: do not wrap around
  const size_t client_width = GetClientRect().GetWidth();
  const size_t available =
      client_width > fixed_field_width ? client_width - fixed_field_width : 0;
  const size_t dyn_width = available / dyn_width_fields.size();
  for (auto *field : dyn_width_fields) {
    field->SetAutoWidth(dyn_width);
  }
}

void wxCustomStatusBar::SetText(const wxString &text) {
  m_text = text;
  UpdateMainTextField();
}

void wxCustomStatusBar::OnMouseMotion(wxMouseEvent &event) {
  event.Skip();
  wxString current_tip = GetToolTipText();
  wxString tip_text;
  wxPoint point = event.GetPosition();
  for (const auto &field : m_fields) {
    if (field->HitTest(point)) {
      tip_text = field->GetTooltip();
      break;
    }
  }

  if (current_tip != tip_text) {
    SetToolTip(tip_text);
  }
}

wxRect wxCustomStatusBar::DoGetMainFieldRect() {
  wxRect rect = GetClientRect();
  size_t offset_x = 0;
  for (const auto &field : m_fields) {
    // Prepare the rect
    if (field.get() == m_mainText.get()) {
      // found the main text field
      break;
    }
    size_t width =
        field->IsAutoWidth() ? field->GetAutoWidth() : field->GetWidth();
    offset_x += width;
  }

  // Calculate the fields length
  wxRect mainRect(offset_x, rect.y,
                  m_mainText->IsAutoWidth() ? m_mainText->GetAutoWidth()
                                            : m_mainText->GetWidth(),
                  rect.height);
  return mainRect;
}
