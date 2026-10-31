#include "app/TabIcon.h"

#include <wx/bmpbndl.h>

namespace {
constexpr int kTabIconDip = 24;

class TabIconImpl : public wxBitmapBundleImpl {
public:
  explicit TabIconImpl(const wxBitmapBundle &source) : m_source(source) {}

  wxSize GetDefaultSize() const override {
    return wxSize(kTabIconDip, kTabIconDip);
  }

  wxSize GetPreferredBitmapSizeAtScale(double scale) const override {
    const int size = wxRound(kTabIconDip * scale);
    return wxSize(size, size);
  }

  wxBitmap GetBitmap(const wxSize &size) override {
    return m_source.GetBitmap(size);
  }

private:
  wxBitmapBundle m_source;
};
} // namespace

wxBitmapBundle TabIcon(const wxBitmapBundle &source) {
  return source.IsOk() ? wxBitmapBundle::FromImpl(new TabIconImpl(source))
                       : wxBitmapBundle{};
}
