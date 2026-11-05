// swift trim-video.swift <in.mov> <out.mp4> <start-seconds> <duration-seconds> [bitrate-kbps]
// Cuts a part of a screen recording and writes it as H.264, 1280x720, 30 fps.
// The default bitrate (1500 kbit/s) is enough for this content: mostly still screenshots.
// (macOS has no ffmpeg; this uses AVFoundation.)
import AVFoundation
import Foundation

let args = CommandLine.arguments
guard args.count >= 5, let start = Double(args[3]), let duration = Double(args[4]) else {
  print("usage: swift trim-video.swift in.mov out.mp4 start duration [kbps]")
  exit(2)
}
let kbps = args.count > 5 ? (Int(args[5]) ?? 1500) : 1500
let asset = AVURLAsset(url: URL(fileURLWithPath: args[1]))
let outURL = URL(fileURLWithPath: args[2])
try? FileManager.default.removeItem(at: outURL)

let sem = DispatchSemaphore(value: 0)
nonisolated(unsafe) var keepAlive: [AnyObject] = []  // the reader and the writer must outlive the task
Task {
  do {
    guard let track = try await asset.loadTracks(withMediaType: .video).first else {
      print("no video track"); exit(1)
    }
    let reader = try AVAssetReader(asset: asset)
    reader.timeRange = CMTimeRange(start: CMTime(seconds: start, preferredTimescale: 600),
                                   duration: CMTime(seconds: duration, preferredTimescale: 600))
    let readerOut = AVAssetReaderTrackOutput(track: track, outputSettings: [
      kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA])
    reader.add(readerOut)

    let writer = try AVAssetWriter(outputURL: outURL, fileType: .mp4)
    let input = AVAssetWriterInput(mediaType: .video, outputSettings: [
      AVVideoCodecKey: AVVideoCodecType.h264,
      AVVideoWidthKey: 1280, AVVideoHeightKey: 720,
      AVVideoScalingModeKey: AVVideoScalingModeResizeAspect,
      AVVideoCompressionPropertiesKey: [
        AVVideoAverageBitRateKey: kbps * 1000,
        AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
        AVVideoMaxKeyFrameIntervalKey: 60,
      ],
    ])
    writer.shouldOptimizeForNetworkUse = true
    writer.add(input)
    keepAlive = [reader, writer, input, readerOut]
    if !reader.startReading() {
      print("cannot read: \(String(describing: reader.error))"); exit(1)
    }
    writer.startWriting()
    writer.startSession(atSourceTime: .zero)
    let first = LockedTime()
    let queue = DispatchQueue(label: "encode")
    input.requestMediaDataWhenReady(on: queue) {
      while input.isReadyForMoreMediaData {
        guard let sample = readerOut.copyNextSampleBuffer() else {
          input.markAsFinished()
          writer.finishWriting { sem.signal() }
          return
        }
        let pts = CMSampleBufferGetPresentationTimeStamp(sample)
        let base = first.set(pts)
        var timing = CMSampleTimingInfo(duration: CMSampleBufferGetDuration(sample),
                                        presentationTimeStamp: pts - base,
                                        decodeTimeStamp: .invalid)
        var shifted: CMSampleBuffer?
        CMSampleBufferCreateCopyWithNewTiming(allocator: nil, sampleBuffer: sample,
                                              sampleTimingEntryCount: 1, sampleTimingArray: &timing,
                                              sampleBufferOut: &shifted)
        if let shifted { input.append(shifted) }
      }
    }
  } catch {
    print("failed: \(error)"); exit(1)
  }
}
final class LockedTime: @unchecked Sendable {
  private var value: CMTime?
  func set(_ t: CMTime) -> CMTime { if value == nil { value = t }; return value! }
}
sem.wait()
print("ok: \(args[2])")
