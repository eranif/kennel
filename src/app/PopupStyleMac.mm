#ifdef __WXOSX__
#import <Cocoa/Cocoa.h>

#include "app/PopupStyle.hpp"

#include <wx/window.h>

// The blur behind a glass background. It takes no mouse clicks: they go to
// the view that it is in, as if it was not there.
@interface KennelGlassView : NSVisualEffectView
@end

@implementation KennelGlassView
- (NSView *)hitTest:(NSPoint)point {
  return nil;
}
@end

namespace {
// Makes the native controls inside the popup draw no background, so the blur
// behind them shows through
void ClearBackgrounds(NSView *view) {
  if ([view isKindOfClass:[NSScrollView class]]) {
    [(NSScrollView *)view setDrawsBackground:NO];
  } else if ([view isKindOfClass:[NSClipView class]]) {
    [(NSClipView *)view setDrawsBackground:NO];
  } else if ([view isKindOfClass:[NSTableView class]]) {
    [(NSTableView *)view setBackgroundColor:[NSColor clearColor]];
  }
  for (NSView *child in [view subviews]) {
    ClearBackgrounds(child);
  }
}
} // namespace

void StylePopupWindow(wxWindow *popup, double radius) {
  if (popup == nullptr) {
    return;
  }

  NSWindow *win = (NSWindow *)popup->MacGetTopLevelWindowRef();
  if (win == nil) {
    return;
  }

  // A transparent window with a shadow; the content view is clipped to the
  // rounded rectangle
  [win setOpaque:NO];
  [win setBackgroundColor:[NSColor clearColor]];
  [win setHasShadow:YES];

  NSView *content = [win contentView];
  content.wantsLayer = YES;
  content.layer.cornerRadius = radius;
  content.layer.masksToBounds = YES;
  content.layer.borderWidth = 1.0;
  content.layer.borderColor = [[NSColor separatorColor] CGColor];

  // Translucent: a blurred view behind all the controls, whose own
  // backgrounds are removed
  NSVisualEffectView *blur =
      [[NSVisualEffectView alloc] initWithFrame:[content bounds]];
  blur.material = NSVisualEffectMaterialHUDWindow;
  blur.blendingMode = NSVisualEffectBlendingModeBehindWindow;
  blur.state = NSVisualEffectStateActive;
  blur.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  ClearBackgrounds(content);
  [content addSubview:blur positioned:NSWindowBelow relativeTo:nil];
  [blur release]; // The view keeps it

  // Recompute the shadow for the new (rounded) shape
  [win invalidateShadow];
}

bool SetGlassBackground(wxWindow *window) {
  if (window == nullptr) {
    return false;
  }
  // A top level window: behind all of its content.
  NSView *view =
      window->IsTopLevel()
          ? [(NSWindow *)window->MacGetTopLevelWindowRef() contentView]
          : (NSView *)window->GetHandle();
  if (view == nil) {
    return false;
  }
  for (NSView *child in [view subviews]) {
    if ([child isKindOfClass:[KennelGlassView class]]) {
      return true; // It has it already
    }
  }
  KennelGlassView *glass =
      [[KennelGlassView alloc] initWithFrame:[view bounds]];
  glass.material = NSVisualEffectMaterialSidebar;
  glass.blendingMode = NSVisualEffectBlendingModeBehindWindow;
  glass.state = NSVisualEffectStateActive;
  glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  ClearBackgrounds(view);
  [view addSubview:glass positioned:NSWindowBelow relativeTo:nil];
  [glass release]; // The view keeps it
  return true;
}
#endif
