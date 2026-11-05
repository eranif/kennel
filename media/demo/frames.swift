import AVFoundation
import AppKit
let a = CommandLine.arguments
let asset = AVURLAsset(url: URL(fileURLWithPath: a[1]))
let gen = AVAssetImageGenerator(asset: asset)
gen.requestedTimeToleranceBefore = .zero; gen.requestedTimeToleranceAfter = .zero
gen.appliesPreferredTrackTransform = true
let sem = DispatchSemaphore(value: 0)
Task {
  let d = try await asset.load(.duration).seconds
  print("duration \(d)")
  for t in a[3...].compactMap({ Double($0) }) {
    do {
      let (img, _) = try await gen.image(at: CMTime(seconds: t, preferredTimescale: 600))
      let rep = NSBitmapImageRep(cgImage: img)
      try rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: "\(a[2])_\(Int(t)).png"))
    } catch { print("fail \(t): \(error)") }
  }
  sem.signal()
}
sem.wait()
