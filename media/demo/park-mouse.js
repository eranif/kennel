// osascript -l JavaScript park-mouse.js : moves the mouse pointer to the bottom right corner of the
// screen, out of the way of a screen recording (needs no permission).
ObjC.import("CoreGraphics");
function run() {
  $.CGWarpMouseCursorPosition($.CGPointMake(100000, 100000));
  return "ok";
}
