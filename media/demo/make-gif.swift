// swift make-gif.swift <in.mp4> <out.gif> <width> <fps> <from-to> [<from-to> ...]
// Writes an animated GIF from parts of the video (the times are in seconds), e.g.
//   swift make-gif.swift kennel-promo.mp4 teaser.gif 720 8 7.5-10 20.5-23.5
// The teaser at the top of the README is made this way. (macOS has no ffmpeg; this uses
// AVFoundation and ImageIO.)
import AVFoundation
import AppKit
import ImageIO
import UniformTypeIdentifiers

let a = CommandLine.arguments
guard a.count >= 6, let width = Int(a[3]), let fps = Double(a[4]) else {
  print("usage: swift make-gif.swift in.mp4 out.gif width fps from-to ...")
  exit(2)
}
var times: [Double] = []
for r in a[5...] {
  let p = r.split(separator: "-").compactMap { Double($0) }
  guard p.count == 2 else { continue }
  var t = p[0]
  while t < p[1] { times.append(t); t += 1 / fps }
}
let asset = AVURLAsset(url: URL(fileURLWithPath: a[1]))
let gen = AVAssetImageGenerator(asset: asset)
gen.requestedTimeToleranceBefore = .zero; gen.requestedTimeToleranceAfter = .zero
gen.appliesPreferredTrackTransform = true
let outURL = URL(fileURLWithPath: a[2])
try? FileManager.default.removeItem(at: outURL)
let dest = CGImageDestinationCreateWithURL(outURL as CFURL, UTType.gif.identifier as CFString, times.count, nil)!
CGImageDestinationSetProperties(dest, [kCGImagePropertyGIFDictionary as String: [kCGImagePropertyGIFLoopCount as String: 0]] as CFDictionary)
let frameProps = [kCGImagePropertyGIFDictionary as String: [kCGImagePropertyGIFDelayTime as String: 1 / fps]] as CFDictionary

let sem = DispatchSemaphore(value: 0)
Task {
  for t in times {
    do {
      let (img, _) = try await gen.image(at: CMTime(seconds: t, preferredTimescale: 600))
      let h = width * img.height / img.width
      let ctx = CGContext(data: nil, width: width, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                          space: CGColorSpaceCreateDeviceRGB(),
                          bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
      ctx.interpolationQuality = .high
      ctx.draw(img, in: CGRect(x: 0, y: 0, width: width, height: h))
      CGImageDestinationAddImage(dest, ctx.makeImage()!, frameProps)
    } catch { print("fail \(t): \(error)") }
  }
  print(CGImageDestinationFinalize(dest) ? "ok: \(times.count) frames" : "failed")
  sem.signal()
}
sem.wait()
