import Foundation
import AVFoundation
import CoreGraphics
import CoreText
import CoreVideo

let output = URL(fileURLWithPath: CommandLine.arguments[1])
let fontURL = URL(fileURLWithPath: CommandLine.arguments[2])
CTFontManagerRegisterFontsForURL(fontURL as CFURL, .process, nil)
let regularURL = URL(fileURLWithPath: CommandLine.arguments[3])
CTFontManagerRegisterFontsForURL(regularURL as CFURL, .process, nil)
try? FileManager.default.removeItem(at: output)
let writer = try AVAssetWriter(outputURL: output, fileType: .mp4)
let width = 1280, height = 720, fps = 30
let input = AVAssetWriterInput(mediaType: .video, outputSettings: [AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: width, AVVideoHeightKey: height, AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: 5_000_000, AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel, AVVideoMaxKeyFrameIntervalKey: 30]])
input.expectsMediaDataInRealTime = false
let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA, kCVPixelBufferWidthKey as String: width, kCVPixelBufferHeightKey as String: height, kCVPixelBufferCGImageCompatibilityKey as String: true, kCVPixelBufferCGBitmapContextCompatibilityKey as String: true])
guard writer.canAdd(input) else { fatalError("cannot add writer input") }
writer.add(input)
guard writer.startWriting() else { fatalError("writer start failed: \(String(describing: writer.error))") }
writer.startSession(atSourceTime: .zero)
let font = CTFontCreateWithName("Lato-Bold" as CFString, 164, nil)
let small = CTFontCreateWithName("Lato-Regular" as CFString, 18, nil)
func smooth(_ t: Double) -> Double { let x = max(0, min(1, t)); return x*x*(3-2*x) }
func line(_ text: String, _ font: CTFont, _ color: CGColor) -> CTLine {
    CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: [NSAttributedString.Key(kCTFontAttributeName as String): font, NSAttributedString.Key(kCTForegroundColorAttributeName as String): color]))
}
let chars = ["l", "c", "a", "l", "s"]
let color = CGColor(gray: 0.96, alpha: 1)
let glyphs = chars.map { line($0, font, color) }
let widths = glyphs.map { CGFloat(CTLineGetTypographicBounds($0, nil, nil, nil)) }
let spacing: CGFloat = 9
let total = widths.reduce(0,+) + CGFloat(chars.count-1)*spacing
let tagline = line("S T U D I O", small, CGColor(gray: 0.67, alpha: 1))
let taglineWidth = CGFloat(CTLineGetTypographicBounds(tagline, nil, nil, nil))
for frame in 0..<150 {
    while !input.isReadyForMoreMediaData { Thread.sleep(forTimeInterval: 0.003) }
    var maybeBuffer: CVPixelBuffer?
    guard let pool = adaptor.pixelBufferPool, CVPixelBufferPoolCreatePixelBuffer(nil, pool, &maybeBuffer) == kCVReturnSuccess, let buffer = maybeBuffer else { fatalError("pixel buffer allocation failed") }
    CVPixelBufferLockBaseAddress(buffer, [])
    guard let ctx = CGContext(data: CVPixelBufferGetBaseAddress(buffer), width: width, height: height, bitsPerComponent: 8, bytesPerRow: CVPixelBufferGetBytesPerRow(buffer), space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue) else { fatalError("context failed") }
    let t = Double(frame)/Double(fps)
    let fade = 1-smooth((t-4.35)/0.65)
    ctx.setFillColor(CGColor(red: 0.025, green: 0.035, blue: 0.055, alpha: 1)); ctx.fill(CGRect(x: 0,y: 0,width: width,height: height))
    ctx.saveGState(); ctx.setAlpha(CGFloat(fade))
    var x = (CGFloat(width)-total)/2
    for i in 0..<glyphs.count {
        let p = smooth((t - 0.45 - Double(i)*0.13)/0.8)
        ctx.saveGState(); ctx.setAlpha(CGFloat(p)); ctx.textPosition = CGPoint(x: x, y: 325 - CGFloat((1-p)*24)); CTLineDraw(glyphs[i], ctx); ctx.restoreGState()
        x += widths[i]+spacing
    }
    let detail = smooth((t-1.45)/0.7)
    ctx.saveGState(); ctx.setAlpha(CGFloat(detail)*0.85)
    ctx.setStrokeColor(CGColor(red: 0.55, green: 0.72, blue: 0.85, alpha: 0.8)); ctx.setLineWidth(1)
    let lineWidth: CGFloat = 196 * CGFloat(smooth((t-1.25)/0.85))
    ctx.move(to: CGPoint(x: CGFloat(width)/2-lineWidth/2, y: 303)); ctx.addLine(to: CGPoint(x: CGFloat(width)/2+lineWidth/2, y: 303)); ctx.strokePath()
    ctx.textPosition = CGPoint(x: (CGFloat(width)-taglineWidth)/2, y: 270); CTLineDraw(tagline, ctx)
    ctx.restoreGState(); ctx.restoreGState()
    CVPixelBufferUnlockBaseAddress(buffer, [])
    guard adaptor.append(buffer, withPresentationTime: CMTime(value: CMTimeValue(frame), timescale: CMTimeScale(fps))) else { fatalError("append failed: \(String(describing: writer.error))") }
}
input.markAsFinished()
let done = DispatchSemaphore(value: 0)
writer.finishWriting { done.signal() }
done.wait()
guard writer.status == .completed else { fatalError("finish failed: \(String(describing: writer.error))") }
print("Created \(output.path)")
