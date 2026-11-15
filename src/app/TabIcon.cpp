#include "app/TabIcon.h"

#include "app/AssetBootstrap.h"

#include <wx/bmpbndl.h>
#include <wx/filename.h>
#include <wx/image.h>

#include <algorithm>
#include <cmath>

namespace {
// Only this file uses the class: the bundle of the icon of a tab.
class TabIconImpl : public wxBitmapBundleImpl {
public:
  static constexpr int kTabIconDip = 16;

  // `dot`: the colour of a dot in the lower right corner, or not ok for none.
  // `overlay`: a picture for the lower right corner, or not ok for none (it
  // comes first).
  TabIconImpl(const wxBitmapBundle &source, const wxColour &dot,
              const wxBitmapBundle &overlay)
      : m_source(source), m_dot(dot), m_overlay(overlay) {}

  wxSize GetDefaultSize() const override {
    return wxSize(kTabIconDip, kTabIconDip);
  }

  wxSize GetPreferredBitmapSizeAtScale(double scale) const override {
    const int size = wxRound(kTabIconDip * scale);
    return wxSize(size, size);
  }

  wxBitmap GetBitmap(const wxSize &size) override {
    const wxBitmap bitmap = m_source.GetBitmap(size);
    if (m_overlay.IsOk()) {
      return DrawOverlay(bitmap);
    }
    return m_dot.IsOk() ? DrawDot(bitmap) : bitmap;
  }

private:
  // Draws the overlay (about two thirds of the icon) in the lower right corner
  // of `bitmap`, over what is there.
  wxBitmap DrawOverlay(const wxBitmap &bitmap) {
    wxImage image = bitmap.ConvertToImage();
    const int side = std::max(1, image.GetWidth() * 10 / kTabIconDip);
    wxImage over = m_overlay.GetBitmap(wxSize(side, side)).ConvertToImage();
    if (!image.IsOk() || !over.IsOk()) {
      return bitmap;
    }
    if (!image.HasAlpha()) {
      image.InitAlpha();
    }
    const int left = image.GetWidth() - over.GetWidth();
    const int top = image.GetHeight() - over.GetHeight();
    for (int y = 0; y < over.GetHeight(); ++y) {
      for (int x = 0; x < over.GetWidth(); ++x) {
        const int dx = left + x;
        const int dy = top + y;
        if (dx < 0 || dy < 0) {
          continue;
        }
        const double cover =
            over.HasAlpha() ? over.GetAlpha(x, y) / 255.0 : 1.0;
        if (cover <= 0.0) {
          continue;
        }
        const double under = image.GetAlpha(dx, dy) / 255.0 * (1.0 - cover);
        const double alpha = cover + under;
        const auto mix = [&](unsigned char above, unsigned char below) {
          return static_cast<unsigned char>(
              std::lround((above * cover + below * under) / alpha));
        };
        image.SetRGB(dx, dy, mix(over.GetRed(x, y), image.GetRed(dx, dy)),
                     mix(over.GetGreen(x, y), image.GetGreen(dx, dy)),
                     mix(over.GetBlue(x, y), image.GetBlue(dx, dy)));
        image.SetAlpha(dx, dy,
                       static_cast<unsigned char>(std::lround(alpha * 255)));
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
        const double under = image.GetAlpha(x, y) / 255.0 * (1.0 - cover);
        const double alpha = cover + under;
        const auto mix = [&](unsigned char over, unsigned char below) {
          return static_cast<unsigned char>(
              std::lround((over * cover + below * under) / alpha));
        };
        image.SetRGB(x, y, mix(m_dot.Red(), image.GetRed(x, y)),
                     mix(m_dot.Green(), image.GetGreen(x, y)),
                     mix(m_dot.Blue(), image.GetBlue(x, y)));
        image.SetAlpha(x, y,
                       static_cast<unsigned char>(std::lround(alpha * 255)));
      }
    }
    return wxBitmap(image);
  }

  wxBitmapBundle m_source;
  wxColour m_dot;
  wxBitmapBundle m_overlay;
};
} // namespace

wxBitmapBundle TabIcon(const wxBitmapBundle &source, bool modified,
                       TabBadge badge) {
  if (!source.IsOk()) {
    return wxBitmapBundle{};
  }
  // The picture of tmux (assets/tmux.svg) marks a session in tmux. Looked for
  // once; without the file, the dot is drawn instead.
  static const wxBitmapBundle tmuxIcon = [] {
    const wxString path = ResolveIconPath("tmux.svg");
    return !path.empty() && wxFileName::FileExists(path)
               ? wxBitmapBundle::FromSVGFile(path, wxSize(24, 24))
               : wxBitmapBundle{};
  }();

  wxColour dot; // None
  wxBitmapBundle overlay;
  if (modified) {
    dot = wxColour(0xF0, 0x8A, 0x1C); // Orange
  } else if (badge == TabBadge::kTmux) {
    overlay = tmuxIcon;
    dot = wxColour(0x2E, 0x9D, 0x8F); // Teal, when there is no picture
  } else if (badge == TabBadge::kDetached) {
    dot = wxColour(0xE6, 0xB8, 0x00); // Yellow
  }
  return wxBitmapBundle::FromImpl(new TabIconImpl(source, dot, overlay));
}
