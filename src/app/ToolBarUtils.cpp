#include "app/ToolBarUtils.h"
#include <wx/settings.h>

namespace toolbar {
namespace {
// Makes the disabled bitmaps when the bundle asks for them: whatever the scale
// of the display, the source bundle gives the bitmap.
class DisabledBundleImpl : public wxBitmapBundleImpl {
public:
  explicit DisabledBundleImpl(const wxBitmapBundle &source)
      : m_source(source) {}

  wxSize GetDefaultSize() const override { return m_source.GetDefaultSize(); }

  wxSize GetPreferredBitmapSizeAtScale(double scale) const override {
    return m_source.GetPreferredBitmapSizeAtScale(scale);
  }

  wxBitmap GetBitmap(const wxSize &size) override {
    const wxBitmap bitmap = m_source.GetBitmap(size);
    if (!bitmap.IsOk()) {
      return bitmap;
    }
    // Fade to the background: black on a dark theme, white on a light one.
    const unsigned char brightness =
        wxSystemSettings::GetAppearance().IsDark() ? 0 : 255;
    // ConvertToDisabled() keeps the size and the scale factor: do not rescale
    // the result, that would give a blurry image on a high DPI display.
    wxBitmap disabled = bitmap.ConvertToDisabled(brightness);
    disabled.SetScaleFactor(bitmap.GetScaleFactor());
    return disabled;
  }

private:
  wxBitmapBundle m_source;
};
} // namespace

wxBitmapBundle MakeDisabledBundle(const wxBitmapBundle &bundle) {
  if (!bundle.IsOk()) {
    return wxBitmapBundle();
  }
  return wxBitmapBundle::FromImpl(new DisabledBundleImpl(bundle));
}

void ApplyStyle(wxAuiToolBar *toolbar) {
  const long style = toolbar->GetWindowStyle();
  const bool dark = wxSystemSettings::GetAppearance().IsDark();
  const long newStyle = dark ? (style & ~wxAUI_TB_PLAIN_BACKGROUND)
                             : (style | wxAUI_TB_PLAIN_BACKGROUND);
  if (newStyle == style) {
    return;
  }
  toolbar->SetWindowStyleFlag(newStyle);
  // Not before the tools are added: Realize() is called by the owner then.
  if (toolbar->GetToolCount() > 0) {
    toolbar->Realize();
    toolbar->Refresh();
  }
}

wxAuiToolBarItem *AddTool(wxAuiToolBar *toolbar, int toolId,
                          const wxString &label, const wxBitmapBundle &bitmap,
                          const wxString &shortHelp, wxItemKind kind) {
  const wxString help = shortHelp.empty() ? label : shortHelp;
  const wxItemKind realKind = kind == wxITEM_DROPDOWN ? wxITEM_NORMAL : kind;
  wxAuiToolBarItem *item =
      toolbar->AddTool(toolId, label, bitmap, MakeDisabledBundle(bitmap),
                       realKind, help, help, nullptr);
  if (kind == wxITEM_DROPDOWN) {
    toolbar->SetToolDropDown(toolId, true);
  }
  return item;
}

wxToolBarToolBase *AddTool(wxToolBarBase *toolbar, int toolId,
                           const wxString &label, const wxBitmapBundle &bitmap,
                           const wxString &shortHelp, wxItemKind kind) {
  const wxString help = shortHelp.empty() ? label : shortHelp;
  return toolbar->AddTool(toolId, label, bitmap, MakeDisabledBundle(bitmap),
                          kind, help, help, nullptr);
}

} // namespace toolbar
