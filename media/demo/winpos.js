// osascript -l JavaScript winpos.js <pid>  ->  "<window number> <x> <y>" of the biggest window of the process.
// Listing windows needs no permission. (Used to find the window to capture.)
ObjC.import("CoreGraphics");
function run(argv) {
  var arr = ObjC.castRefToObject($.CGWindowListCopyWindowInfo(1, 0));
  var pid = parseInt(argv[0]), best = null, area = 0;
  for (var i = 0; i < arr.count; i++) {
    var w = ObjC.deepUnwrap(arr.objectAtIndex(i));
    if (w.kCGWindowOwnerPID == pid) {
      var a = w.kCGWindowBounds.Width * w.kCGWindowBounds.Height;
      if (a > area) { area = a; best = w; }
    }
  }
  return best.kCGWindowNumber + " " + best.kCGWindowBounds.X + " " + best.kCGWindowBounds.Y;
}
