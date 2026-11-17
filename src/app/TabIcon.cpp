#include "app/TabIcon.h"

#include <wx/bmpbndl.h>
#include <wx/image.h>

#include <algorithm>
#include <cmath>

namespace {
// Only this file uses the class: the bundle of the icon of a tab.
class TabIconImpl : public wxBitmapBundleImpl {
public:
  static constexpr int kTabIconDip = 16;
  // The strip on the left of the icon: its width, and the room between it and
  // the icon.
  static constexpr int kStripDip = 2;
  static constexpr int kStripGapDip = 2;

  // `dot`: the colour of a dot in the lower right corner, or not ok for none.
  // `strip`: the colour of a strip on the left of the icon, or not ok for none.
  // The strip makes the bitmap wider: the icon keeps its size.
  TabIconImpl(const wxBitmapBundle &source, const wxColour &dot,
              const wxColour &strip)
      : m_source(source), m_dot(dot), m_strip(strip) {}

  wxSize GetDefaultSize() const override { return GetSizeAtScale(1.0); }

  wxSize GetPreferredBitmapSizeAtScale(double scale) const override {
    return GetSizeAtScale(scale);
  }

  wxBitmap GetBitmap(const wxSize &size) override {
    if (m_strip.IsOk()) {
      return DrawStrip(size);
    }
    const wxBitmap bitmap = m_source.GetBitmap(size);
    return m_dot.IsOk() ? DrawDot(bitmap) : bitmap;
  }

private:
  // The size of the bitmap at `scale`: the icon, and the strip on its left when
  // there is one.
  wxSize GetSizeAtScale(double scale) const {
    const int side = wxRound(kTabIconDip * scale);
    const int strip =
        m_strip.IsOk() ? wxRound((kStripDip + kStripGapDip) * scale) : 0;
    return wxSize(side + strip, side);
  }

  // Puts `colour` over the pixel (x, y) of `image`: `cover` (0 to 1) says how
  // much of the pixel it takes.
  static void BlendPixel(wxImage &image, int x, int y, const wxColour &colour,
                         double cover) {
    const double under = image.GetAlpha(x, y) / 255.0 * (1.0 - cover);
    const double alpha = cover + under;
    const auto mix = [&](unsigned char above, unsigned char below) {
      return static_cast<unsigned char>(
          std::lround((above * cover + below * under) / alpha));
    };
    image.SetRGB(x, y, mix(colour.Red(), image.GetRed(x, y)),
                 mix(colour.Green(), image.GetGreen(x, y)),
                 mix(colour.Blue(), image.GetBlue(x, y)));
    image.SetAlpha(x, y, static_cast<unsigned char>(std::lround(alpha * 255)));
  }

  // A bitmap of `size`: a vertical strip with round ends (and smooth edges) on
  // the left, kStripDip wide and as high as the icon without a margin of 1 DIP
  // at each end, then the icon on the right, as high as the bitmap.
  wxBitmap DrawStrip(const wxSize &size) {
    const double dip = static_cast<double>(size.GetHeight()) / kTabIconDip;
    const int stripRoom = wxRound((kStripDip + kStripGapDip) * dip);
    const int side =
        std::max(1, std::min(size.GetHeight(), size.GetWidth() - stripRoom));
    wxImage image(size);
    image.InitAlpha();
    std::fill_n(image.GetAlpha(), size.GetWidth() * size.GetHeight(), 0);
    const wxImage icon =
        m_source.GetBitmap(wxSize(side, side)).ConvertToImage();
    if (icon.IsOk()) {
      image.Paste(icon, size.GetWidth() - icon.GetWidth(),
                  (size.GetHeight() - icon.GetHeight()) / 2,
                  wxIMAGE_ALPHA_BLEND_COMPOSE);
    }
    const double radius = std::max(0.5, kStripDip * dip / 2); // Half the width
    const double cx = radius;
    // The ends of the line in the middle of the strip.
    const double top = dip + radius;
    const double bottom = size.GetHeight() - dip - radius;
    for (int y = 0; y < size.GetHeight(); ++y) {
      for (int x = 0; x < stripRoom && x < size.GetWidth(); ++x) {
        const double px = x + 0.5;
        const double py = y + 0.5;
        const double nearest = std::clamp(py, top, bottom);
        // How much of the pixel is inside the strip
        const double cover = std::clamp(
            radius + 0.5 - std::hypot(px - cx, py - nearest), 0.0, 1.0);
        if (cover <= 0.0) {
          continue;
        }
        BlendPixel(image, x, y, m_strip, cover);
      }
    }
    return wxBitmap(image);
  }

  // Draws a dot (with smooth edges) in the lower right corner of `bitmap`.
  wxBitmap DrawDot(const wxBitmap &bitmap) {
    wxImage image = bitmap.ConvertToImage();
    if (!image.IsOk()) {
      return bitmap;
    }
    if (!image.HasAlpha()) {
      image.InitAlpha();
    }
    const double radius = image.GetWidth() * 0.24;
    const double cx = image.GetWidth() - radius - 0.5;
    const double cy = image.GetHeight() - radius - 0.5;
    for (int y = 0; y < image.GetHeight(); ++y) {
      for (int x = 0; x < image.GetWidth(); ++x) {
        // How much of the pixel is inside the dot
        const double cover = std::clamp(
            radius + 0.5 - std::hypot(x + 0.5 - cx, y + 0.5 - cy), 0.0, 1.0);
        if (cover <= 0.0) {
          continue;
        }
        BlendPixel(image, x, y, m_dot, cover);
      }
    }
    return wxBitmap(image);
  }

  wxBitmapBundle m_source;
  wxColour m_dot;
  wxColour m_strip;
};
} // namespace

wxBitmapBundle TabIcon(const wxBitmapBundle &source, bool modified,
                       TabBadge badge) {
  if (!source.IsOk()) {
    return wxBitmapBundle{};
  }
  wxColour dot;   // None
  wxColour strip; // None
  if (modified) {
    dot = wxColour(0xF0, 0x8A, 0x1C); // Orange
  } else if (badge == TabBadge::kTmux) {
    strip = wxColour(0x3F, 0xB9, 0x50); // Green
  } else if (badge == TabBadge::kDetached) {
    dot = wxColour(0xE6, 0xB8, 0x00); // Yellow
  }
  return wxBitmapBundle::FromImpl(new TabIconImpl(source, dot, strip));
}
