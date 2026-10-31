#include "app/TabIcon.h"

#include <wx/bmpbndl.h>
#include <wx/image.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kTabIconDip = 24;

// Draws a dot (with smooth edges) in the lower right corner of `bitmap`.
wxBitmap WithModifiedDot(const wxBitmap &bitmap) {
  wxImage image = bitmap.ConvertToImage();
  if (!image.IsOk()) {
    return bitmap;
  }
  if (!image.HasAlpha()) {
    image.InitAlpha();
  }
  const wxColour dot(0xF0, 0x8A, 0x1C);
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
      image.SetRGB(x, y, mix(dot.Red(), image.GetRed(x, y)),
                   mix(dot.Green(), image.GetGreen(x, y)),
                   mix(dot.Blue(), image.GetBlue(x, y)));
      image.SetAlpha(x, y,
                     static_cast<unsigned char>(std::lround(alpha * 255)));
    }
  }
  return wxBitmap(image);
}

class TabIconImpl : public wxBitmapBundleImpl {
public:
  TabIconImpl(const wxBitmapBundle &source, bool modified)
      : m_source(source), m_modified(modified) {}

  wxSize GetDefaultSize() const override {
    return wxSize(kTabIconDip, kTabIconDip);
  }

  wxSize GetPreferredBitmapSizeAtScale(double scale) const override {
    const int size = wxRound(kTabIconDip * scale);
    return wxSize(size, size);
  }

  wxBitmap GetBitmap(const wxSize &size) override {
    const wxBitmap bitmap = m_source.GetBitmap(size);
    return m_modified ? WithModifiedDot(bitmap) : bitmap;
  }

private:
  wxBitmapBundle m_source;
  bool m_modified;
};
} // namespace

wxBitmapBundle TabIcon(const wxBitmapBundle &source, bool modified) {
  return source.IsOk()
             ? wxBitmapBundle::FromImpl(new TabIconImpl(source, modified))
             : wxBitmapBundle{};
}
