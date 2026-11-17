#!/usr/bin/env swift
// macOS, no third-party dependencies. Called by capture_watch_demo.mjs.
import Foundation
import AVFoundation
import CoreGraphics
import CoreVideo
import ImageIO

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data((message + "\n").utf8))
    exit(1)
}

var options: [String: String] = [:]
var index = 1
while index < CommandLine.arguments.count {
    let key = CommandLine.arguments[index]
    guard key.hasPrefix("--"), index + 1 < CommandLine.arguments.count else {
        fail("Usage: swift encode_watch_demo.swift --input frames --output animation.mp4 --fps 30 [--background 0f151a]")
    }
    options[key] = CommandLine.arguments[index + 1]
    index += 2
}
guard let input = options["--input"], let output = options["--output"],
      let fps = Int32(options["--fps"] ?? "30"), fps > 0 else {
    fail("Provide --input, --output, and a positive --fps.")
}
let backgroundHex = (options["--background"] ?? "0f151a").replacingOccurrences(of: "#", with: "")
guard backgroundHex.count == 6, let background = UInt32(backgroundHex, radix: 16) else {
    fail("--background must be an RGB hex color, e.g. 0f151a.")
}
let fileManager = FileManager.default
let inputURL = URL(fileURLWithPath: input, isDirectory: true)
let files: [URL]
do {
    files = try fileManager.contentsOfDirectory(at: inputURL, includingPropertiesForKeys: nil)
        .filter { $0.lastPathComponent.hasPrefix("frame-") && $0.pathExtension.lowercased() == "png" }
        .sorted { $0.lastPathComponent < $1.lastPathComponent }
} catch { fail("Cannot read frame directory: \(error)") }
guard let first = files.first,
      let source = CGImageSourceCreateWithURL(first as CFURL, nil),
      let firstImage = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
    fail("No readable frame-*.png images in \(input).")
}
let width = firstImage.width
let height = firstImage.height
guard width > 0 && height > 0 && width % 2 == 0 && height % 2 == 0 else {
    fail("H.264 output requires positive, even frame dimensions (found \(width)×\(height)).")
}
let outputURL = URL(fileURLWithPath: output)
do {
    try fileManager.createDirectory(at: outputURL.deletingLastPathComponent(), withIntermediateDirectories: true)
    if fileManager.fileExists(atPath: output) { try fileManager.removeItem(at: outputURL) }
} catch { fail("Cannot prepare output: \(error)") }
let writer: AVAssetWriter
do { writer = try AVAssetWriter(outputURL: outputURL, fileType: .mp4) }
catch { fail("Cannot create MP4 writer: \(error)") }
writer.shouldOptimizeForNetworkUse = true
let videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: [
    AVVideoCodecKey: AVVideoCodecType.h264,
    AVVideoWidthKey: width,
    AVVideoHeightKey: height,
    AVVideoColorPropertiesKey: [
        AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
        AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
        AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2,
    ],
    AVVideoCompressionPropertiesKey: [
        AVVideoAverageBitRateKey: max(1_500_000, width * height * 5),
        AVVideoExpectedSourceFrameRateKey: fps,
        AVVideoMaxKeyFrameIntervalKey: fps * 2,
        AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
        AVVideoAllowFrameReorderingKey: false,
    ],
])
videoInput.expectsMediaDataInRealTime = false
let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: videoInput, sourcePixelBufferAttributes: [
    kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
    kCVPixelBufferWidthKey as String: width,
    kCVPixelBufferHeightKey as String: height,
    kCVPixelBufferCGImageCompatibilityKey as String: true,
    kCVPixelBufferCGBitmapContextCompatibilityKey as String: true,
])
guard writer.canAdd(videoInput) else { fail("Cannot add H.264 video input.") }
writer.add(videoInput)
guard writer.startWriting() else { fail("Cannot start writing: \(String(describing: writer.error))") }
writer.startSession(atSourceTime: .zero)
guard let pool = adaptor.pixelBufferPool else { fail("Cannot allocate pixel buffer pool.") }
let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
let bitmapInfo = CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue
for (number, file) in files.enumerated() {
    autoreleasepool {
        guard let source = CGImageSourceCreateWithURL(file as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil),
              image.width == width && image.height == height else {
            fail("Frame is unreadable or has a different size: \(file.lastPathComponent)")
        }
        var optionalBuffer: CVPixelBuffer?
        guard CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &optionalBuffer) == kCVReturnSuccess,
              let buffer = optionalBuffer else { fail("Cannot allocate video frame.") }
        CVBufferSetAttachment(buffer, kCVImageBufferColorPrimariesKey,
                              kCVImageBufferColorPrimaries_ITU_R_709_2, .shouldPropagate)
        CVBufferSetAttachment(buffer, kCVImageBufferTransferFunctionKey,
                              kCVImageBufferTransferFunction_ITU_R_709_2, .shouldPropagate)
        CVBufferSetAttachment(buffer, kCVImageBufferYCbCrMatrixKey,
                              kCVImageBufferYCbCrMatrix_ITU_R_709_2, .shouldPropagate)
        CVPixelBufferLockBaseAddress(buffer, [])
        guard let context = CGContext(data: CVPixelBufferGetBaseAddress(buffer), width: width, height: height,
                                      bitsPerComponent: 8, bytesPerRow: CVPixelBufferGetBytesPerRow(buffer),
                                      space: colorSpace, bitmapInfo: bitmapInfo) else {
            fail("Cannot create video drawing context.")
        }
        context.setFillColor(red: CGFloat((background >> 16) & 255) / 255,
                             green: CGFloat((background >> 8) & 255) / 255,
                             blue: CGFloat(background & 255) / 255, alpha: 1)
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
        CVPixelBufferUnlockBaseAddress(buffer, [])
        let deadline = Date().addingTimeInterval(30)
        while !videoInput.isReadyForMoreMediaData {
            if writer.status == .failed || Date() > deadline {
                fail("Video encoder stalled: \(String(describing: writer.error))")
            }
            Thread.sleep(forTimeInterval: 0.001)
        }
        guard adaptor.append(buffer, withPresentationTime: CMTime(value: Int64(number), timescale: fps)) else {
            fail("Cannot append frame \(number): \(String(describing: writer.error))")
        }
    }
}
writer.endSession(atSourceTime: CMTime(value: Int64(files.count), timescale: fps))
videoInput.markAsFinished()
let finished = DispatchSemaphore(value: 0)
writer.finishWriting { finished.signal() }
guard finished.wait(timeout: .now() + 60) == .success, writer.status == .completed else {
    fail("MP4 completion failed: \(String(describing: writer.error))")
}
print("Encoded \(files.count) frames, \(width)×\(height), \(fps) fps → \(output)")
