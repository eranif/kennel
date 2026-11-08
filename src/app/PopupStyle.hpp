#pragma once

class wxWindow;

#ifdef __WXMAC__
constexpr double kDefaultPopupRadius = 5.0;
#else
constexpr double kDefaultPopupRadius = 10.0;
#endif

// macOS and Windows: gives a top level popup window a drop shadow and rounded
// corners. Does nothing on other platforms. Call it after the popup is shown
// (the native window must exist). On Windows 11 the system draws the corners
// and supports two sizes only (about 8 and 4 pixels at 100% scaling), so
// `radius` selects the closest one. On older versions of Windows the window is
// clipped to a rounded rectangle and it has no shadow.
void StylePopupWindow(wxWindow *popup, double radius = kDefaultPopupRadius);

// Gives `window` a glass background: the blurred desktop shows behind it, and
// its content stays opaque (text and icons stay sharp). Call it after the
// native window exists. Returns false when the platform cannot do it.
//
// The glass shows only where nothing paints over it: the views of `window`
// (wx panels, custom drawing) must leave their background clear or paint it
// with alpha. The native controls inside `window` when this is called stop
// drawing their own background (macOS).
//
// macOS: any window (a top level window, or a child view). Windows 11 (22H2
// and later): top level windows only, with the acrylic system backdrop. Other
// platforms: not supported.
bool SetGlassBackground(wxWindow *window);
