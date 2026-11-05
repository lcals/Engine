import AVFoundation
import Foundation
let video = AVURLAsset(url: URL(fileURLWithPath: CommandLine.arguments[1]))
let audio = AVURLAsset(url: URL(fileURLWithPath: CommandLine.arguments[2]))
let destination = URL(fileURLWithPath: CommandLine.arguments[3])
try? FileManager.default.removeItem(at: destination)
let composition = AVMutableComposition()
let five = CMTime(seconds: 5, preferredTimescale: 600)
let v = composition.addMutableTrack(withMediaType: .video, preferredTrackID: kCMPersistentTrackID_Invalid)!
try v.insertTimeRange(CMTimeRange(start: .zero, duration: five), of: video.tracks(withMediaType: .video).first!, at: .zero)
let a = composition.addMutableTrack(withMediaType: .audio, preferredTrackID: kCMPersistentTrackID_Invalid)!
try a.insertTimeRange(CMTimeRange(start: .zero, duration: five), of: audio.tracks(withMediaType: .audio).first!, at: .zero)
guard let exporter = AVAssetExportSession(asset: composition, presetName: AVAssetExportPresetHighestQuality) else { fatalError("cannot create exporter") }
exporter.outputFileType = .mp4
exporter.outputURL = destination
let semaphore = DispatchSemaphore(value: 0)
exporter.exportAsynchronously { semaphore.signal() }
semaphore.wait()
guard exporter.status == .completed else { fatalError("export failed: \(String(describing: exporter.error))") }
print(destination.path)
