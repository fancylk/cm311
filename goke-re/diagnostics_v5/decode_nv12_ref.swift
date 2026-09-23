import AVFoundation
import Foundation
let asset = AVAsset(url: URL(fileURLWithPath: CommandLine.arguments[1]))
let track = asset.tracks(withMediaType: .video).first!
let reader = try AVAssetReader(asset: asset)
let output = AVAssetReaderTrackOutput(track: track, outputSettings: [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange])
reader.add(output)
reader.startReading()
var n=0
while let sb=output.copyNextSampleBuffer() {
    if n==30, let pb=CMSampleBufferGetImageBuffer(sb) {
        CVPixelBufferLockBaseAddress(pb, .readOnly)
        for i in 0..<2 {
            let h=CVPixelBufferGetHeightOfPlane(pb,i), stride=CVPixelBufferGetBytesPerRowOfPlane(pb,i)
            let data=Data(bytes: CVPixelBufferGetBaseAddressOfPlane(pb,i)!, count: h*stride)
            try data.write(to: URL(fileURLWithPath: CommandLine.arguments[2]+(i==0 ? ".y" : ".uv")))
            print("reference plane",i,CVPixelBufferGetWidthOfPlane(pb,i),h,stride)
        }
        CVPixelBufferUnlockBaseAddress(pb, .readOnly)
        break
    }
    n += 1
}
