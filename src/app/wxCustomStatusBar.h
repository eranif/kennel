#pragma once

// A status bar made of fields drawn by hand: a main text area that takes the
// free space, and fixed-width text and icon + text fields. Adapted from
// CodeLite's wxCustomStatusBar (same author).

#include <memory>
#include <vector>
#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/event.h>
#include <wx/statusbr.h>

class wxCustomStatusBar;

// The colours and the primitives the fields draw with.
class wxCustomStatusBarArt {
public:
  using Ptr_t = std::shared_ptr<wxCustomStatusBarArt>;

  virtual ~wxCustomStatusBarArt() = default;

  virtual void DrawText(wxDC &dc, wxCoord x, wxCoord y, const wxString &text);
  virtual void DrawFieldSeparator(wxDC &dc, const wxRect &fieldRect);

  virtual wxColour GetBgColour() const;
  virtual wxColour GetPenColour() const;
  virtual wxColour GetTextColour() const;
};

//================---------------
// Base field
//================---------------
class wxCustomStatusBarField {
public:
  using Ptr_t = std::shared_ptr<wxCustomStatusBarField>;
  using Vect_t = std::vector<wxCustomStatusBarField::Ptr_t>;

  explicit wxCustomStatusBarField(wxCustomStatusBar *parent)
      : m_parent(parent) {}
  virtual ~wxCustomStatusBarField() = default;

  // An auto width field gets an equal share of what the fixed width fields
  // leave (see wxCustomStatusBar::Finalize()).
  bool IsAutoWidth() const { return m_autoWidthEnabled; }
  void SetAutoWidthEnabled(bool b) { m_autoWidthEnabled = b; }
  void SetAutoWidth(size_t w) { m_autoWidth = w; }
  size_t GetAutoWidth() const { return m_autoWidth; }

  /**
   * @brief render the field content
   * @param dc the device content
   * @param rect the field bounding rect
   */
  virtual void Render(wxDC &dc, const wxRect &rect,
                      wxCustomStatusBarArt::Ptr_t art) = 0;

  /**
   * @brief return the field length
   */
  virtual size_t GetWidth() const = 0;

  /**
   * @brief return true if 'point' is inside the current field
   * @param point position in parent coordinates
   */
  bool HitTest(const wxPoint &point) const;

  void SetTooltip(const wxString &tooltip) { m_tooltip = tooltip; }
  const wxString &GetTooltip() const { return m_tooltip; }
  const wxRect &GetRect() const { return m_rect; }
  void SetRect(const wxRect &rect) { m_rect = rect; }

protected:
  wxRect m_rect;
  wxString m_tooltip;
  wxCustomStatusBar *m_parent;
  bool m_autoWidthEnabled{false};
  size_t m_autoWidth{0};
};

//================---------------
// Text field
//================---------------
class wxCustomStatusBarFieldText : public wxCustomStatusBarField {
public:
  wxCustomStatusBarFieldText(wxCustomStatusBar *parent, size_t width)
      : wxCustomStatusBarField(parent), m_width(width) {}

  void Render(wxDC &dc, const wxRect &rect,
              wxCustomStatusBarArt::Ptr_t art) override;

  void SetText(const wxString &text);
  const wxString &GetText() const { return m_text; }

  void SetWidth(size_t width) { m_width = width; }
  size_t GetWidth() const override { return m_width; }

  void SetTextAlignment(wxAlignment align) { m_textAlign = align; }

private:
  wxString m_text;
  size_t m_width;
  wxAlignment m_textAlign{wxALIGN_CENTER};
};

//================---------------
// Bitmap field
//================---------------
// An optional icon, then a label.
class wxCustomStatusBarBitmapField : public wxCustomStatusBarField {
public:
  wxCustomStatusBarBitmapField(wxCustomStatusBar *parent, size_t width)
      : wxCustomStatusBarField(parent), m_width(width) {}

  void Render(wxDC &dc, const wxRect &rect,
              wxCustomStatusBarArt::Ptr_t art) override;

  void SetWidth(size_t width) { m_width = width; }
  size_t GetWidth() const override { return m_width; }

  void SetBitmap(const wxBitmap &bitmap) { m_bitmap = bitmap; }
  const wxBitmap &GetBitmap() const { return m_bitmap; }

  void SetLabel(const wxString &label) { m_label = label; }
  const wxString &GetLabel() const { return m_label; }

  // The width that shows the icon, a label `labelWidth` pixels wide and the
  // space around them.
  size_t GetBestWidth(int labelWidth) const;

private:
  size_t m_width;
  wxBitmap m_bitmap;
  wxString m_label;
};

//================---------------
// Custom status bar
//================---------------
class wxCustomStatusBar : public wxStatusBar {
public:
  wxCustomStatusBar(wxWindow *parent, wxWindowID id = wxID_ANY, long style = 0);
  ~wxCustomStatusBar() override = default;

  /// Add field to the status bar, returns its index. The main text area is
  /// always the first field.
  size_t AddField(wxCustomStatusBarField::Ptr_t field) {
    m_fields.push_back(field);
    return m_fields.size() - 1;
  }

  /// Gives the fields without a fixed width their share of the bar. Called
  /// before every paint.
  void Finalize();

  wxCustomStatusBarArt::Ptr_t GetArt() { return m_art; }

  /**
   * @brief the width in pixels of `text` as the bar paints it
   */
  int GetTextWidth(const wxString &text) const;

  /**
   * @brief set the text of the main text area
   */
  void SetText(const wxString &text);

  /**
   * @brief the text of the main text area
   */
  const wxString &GetText() const { return m_text; }

private:
  void OnPaint(wxPaintEvent &event);
  void OnEraseBackground(wxEraseEvent &event);
  void OnMouseMotion(wxMouseEvent &event);
  wxRect DoGetMainFieldRect();
  void UpdateMainTextField();

  wxCustomStatusBarArt::Ptr_t m_art;
  wxCustomStatusBarField::Vect_t m_fields;
  wxString m_text; // shown in the main text area
  std::shared_ptr<wxCustomStatusBarFieldText> m_mainText;
};
