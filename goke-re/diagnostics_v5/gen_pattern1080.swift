// Generate H264 + HEVC test streams (annexb) via AVFoundation hardware encoders.
// Output per codec: <codec>_params.bin (annexb VPS/SPS/PPS), <codec>_frames.bin (annexb AUs),
//                   <codec>_index.bin (u32 LE per-AU byte lengths), <codec>_key0.txt ("1" if AU0 is IDR)
import AVFoundation
import CoreMedia
import Foundation
import CoreText

func generate(codec: String, out dir: String, width: Int, height: Int) throws {
    let fps = 30, nFrames = 90
    let codecType: AVVideoCodecType = codec == "hevc" ? .hevc : .h264
    let url = URL(fileURLWithPath: "\(dir)/test_\(codec).mp4")
    try? FileManager.default.removeItem(at: url)
    let writer = try AVAssetWriter(outputURL: url, fileType: .mp4)
    let settings: [String: Any] = [
        AVVideoCodecKey: codecType,
        AVVideoWidthKey: width,
        AVVideoHeightKey: height,
        AVVideoCompressionPropertiesKey: [
            AVVideoAverageBitRateKey: 2_000_000,
            AVVideoMaxKeyFrameIntervalKey: 30,
        ],
    ]
    let input = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
    input.expectsMediaDataInRealTime = false
    let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input,
        sourcePixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
            kCVPixelBufferWidthKey as String: width,
            kCVPixelBufferHeightKey as String: height,
        ])
    guard writer.canAdd(input) else { fatalError("cannot add input") }
    writer.add(input)
    guard writer.startWriting() else { fatalError("startWriting failed") }
    writer.startSession(atSourceTime: .zero)

    for i in 0..<nFrames {
        while !input.isReadyForMoreMediaData { usleep(2000) }
        var pb: CVPixelBuffer?
        let attrs: [String: Any] = [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: width,
            kCVPixelBufferHeightKey as String: height,
        ]
        CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_32BGRA, attrs as CFDictionary, &pb)
        let pbu = pb!
        CVPixelBufferLockBaseAddress(pbu, [])
        let ctx = CGContext(data: CVPixelBufferGetBaseAddress(pbu), width: width, height: height,
            bitsPerComponent: 8, bytesPerRow: CVPixelBufferGetBytesPerRow(pbu),
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.noneSkipFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue)!
        let g = CGFloat(0.15 + 0.5 * Double(i % 30) / 30.0)
        ctx.setFillColor(CGColor(gray: g, alpha: 1))
        ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
        let x = CGFloat((i * 12) % width)
        let y = CGFloat((i * 7) % height)
        ctx.setFillColor(red: 0.9, green: 0.2, blue: 0.1, alpha: 1)
        ctx.fill(CGRect(x: x, y: y, width: 200, height: 200))
        ctx.setFillColor(red: 0.1, green: 0.3, blue: 0.9, alpha: 1)
        let x2 = CGFloat(width) - x - 150
        let y2 = CGFloat(height) - y - 150
        ctx.fill(CGRect(x: x2, y: y2, width: 150, height: 150))
        let font = CTFontCreateWithName("PingFangSC-Regular" as CFString, 20, nil)
        let color = CGColor(gray: 0.03, alpha: 1)
        let textAttrs: [CFString: Any] = [kCTFontAttributeName: font, kCTForegroundColorAttributeName: color]
        for row in 0..<12 {
            let line = "CM311 GK6323 RustDesk H265 1080p 测试文字 abc 0123456789 行号 \(row)" as CFString
            let attributed = CFAttributedStringCreate(kCFAllocatorDefault, line, textAttrs as CFDictionary)!
            ctx.textPosition = CGPoint(x: 180, y: 120 + row * 63)
            CTLineDraw(CTLineCreateWithAttributedString(attributed), ctx)
        }
        for band in 0..<(height / 8) {
            switch band % 4 {
            case 0: ctx.setFillColor(red: 0.95, green: 0.15, blue: 0.10, alpha: 1)
            case 1: ctx.setFillColor(red: 0.10, green: 0.85, blue: 0.15, alpha: 1)
            case 2: ctx.setFillColor(red: 0.10, green: 0.20, blue: 0.95, alpha: 1)
            default: ctx.setFillColor(red: 0.90, green: 0.85, blue: 0.10, alpha: 1)
            }
            ctx.fill(CGRect(x: 960, y: band * 8, width: width - 960, height: 8))
        }
        CVPixelBufferUnlockBaseAddress(pbu, [])
        adaptor.append(pbu, withPresentationTime: CMTime(value: CMTimeValue(i), timescale: CMTimeScale(fps)))
    }
    input.markAsFinished()
    writer.finishWriting { }
    while writer.status == .writing { usleep(10000) }
    guard writer.status == .completed else { fatalError("write failed: \(String(describing: writer.error))") }
    print("encoded \(codec) mp4 ok")

    // read back as compressed sample buffers
    let asset = AVAsset(url: url)
    let track = asset.tracks(withMediaType: .video).first!
    let reader = try AVAssetReader(asset: asset)
    let out = AVAssetReaderTrackOutput(track: track, outputSettings: nil) // compressed passthrough
    reader.add(out)
    reader.startReading()

    var params: Data = Data()
    var frames: [Data] = []
    var isKey: [Bool] = []
    var sawSync = false
    var formatDesc: CMVideoFormatDescription?
    while let sb = out.copyNextSampleBuffer() {
        if formatDesc == nil { formatDesc = CMSampleBufferGetFormatDescription(sb) }
        var mode: CMAttachmentMode = 0
        let notSyncVal = CMGetAttachment(sb, key: kCMSampleAttachmentKey_NotSync, attachmentModeOut: &mode) as CFTypeRef?
        let sync = notSyncVal == nil
        if sync { sawSync = true }

        guard let bb = CMSampleBufferGetDataBuffer(sb) else { continue }
        var len = 0; var ptr: UnsafeMutablePointer<Int8>?
        CMBlockBufferGetDataPointer(bb, atOffset: 0, lengthAtOffsetOut: nil, totalLengthOut: &len, dataPointerOut: &ptr)
        let raw = Data(bytes: ptr!, count: len)
        // length-prefixed NALUs -> annexb
        var au = Data()
        var off = 0
        while off + 4 <= raw.count {
            let n = (Int(raw[off]) << 24) | (Int(raw[off+1]) << 16) | (Int(raw[off+2]) << 8) | Int(raw[off+3])
            au.append(contentsOf: [0, 0, 0, 1])
            au.append(raw[(off+4)..<(off+4+n)])
            off += 4 + n
        }
        frames.append(au)
        isKey.append(sync)
    }
    guard let fd = formatDesc else { fatalError("no format desc") }

    // parameter sets from format description
    if codec == "hevc" {
        var cnt: Int = 0
        CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fd, parameterSetIndex: 0, parameterSetPointerOut: nil, parameterSetSizeOut: nil, parameterSetCountOut: &cnt, nalUnitHeaderLengthOut: nil)
        for i in 0..<cnt {
            var ps: UnsafePointer<UInt8>? = nil
            var psSize: Int = 0
            if CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fd, parameterSetIndex: i, parameterSetPointerOut: &ps, parameterSetSizeOut: &psSize, parameterSetCountOut: nil, nalUnitHeaderLengthOut: nil) == noErr {
                params.append(contentsOf: [0, 0, 0, 1]); params.append(Data(bytes: ps!, count: psSize))
            }
        }
        print("hevc param sets: \(cnt)")
    } else {
        var ps: UnsafePointer<UInt8>? = nil
        var psSize: Int = 0
        var cnt = 0
        var nalLen: Int32 = 0
        for i in 0..<4 {
            if CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fd, parameterSetIndex: i, parameterSetPointerOut: &ps, parameterSetSizeOut: &psSize, parameterSetCountOut: &cnt, nalUnitHeaderLengthOut: &nalLen) == noErr {
                params.append(contentsOf: [0, 0, 0, 1]); params.append(Data(bytes: ps!, count: psSize))
            } else { break }
        }
        print("h264 param sets: \(cnt)")
    }

    var index = Data()
    for f in frames { var l = UInt32(f.count).littleEndian; withUnsafeBytes(of: &l) { index.append(contentsOf: $0) } }
    try params.write(to: URL(fileURLWithPath: "\(dir)/\(codec)_params.bin"))
    try Data(frames.joined()).write(to: URL(fileURLWithPath: "\(dir)/\(codec)_frames.bin"))
    try index.write(to: URL(fileURLWithPath: "\(dir)/\(codec)_index.bin"))
    try String(isKey.first == true ? "1" : "0").write(to: URL(fileURLWithPath: "\(dir)/\(codec)_key0.txt"), atomically: true, encoding: .utf8)
    print("\(codec): \(frames.count) AUs, au0 sync=\(isKey.first ?? false), params=\(params.count)B, total=\(frames.reduce(0){$0+$1.count})B")
}

let dir = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."
let wh = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "1280x720"
let parts = wh.split(separator: "x")
let W = Int(parts[0])!, H = Int(parts[1])!
let tag = CommandLine.arguments.count > 3 ? CommandLine.arguments[3] : ""
let fm = FileManager.default
try generate(codec: "hevc", out: dir, width: W, height: H)
if !tag.isEmpty {
    for f in ["params","frames","index"] {
        try? fm.moveItem(atPath: "\(dir)/hevc_\(f).bin", toPath: "\(dir)/\(tag)_\(f).bin")
    }
}
