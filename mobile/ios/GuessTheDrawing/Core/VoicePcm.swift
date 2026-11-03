import Foundation

enum VoicePcm {
    static let sampleRate = 16000
    static let frameBytes = 640
    static func decode(_ text: String) -> Data? {
        guard text.utf8.count == 856, let data = Data(base64Encoded: text),
              data.count == frameBytes, data.base64EncodedString() == text else { return nil }
        return data
    }
}
struct VoicePacketizer {
    private var pending = Data()
    mutating func append(_ data: Data) -> [Data] {
        pending.append(data)
        var frames: [Data] = []
        while pending.count >= VoicePcm.frameBytes {
            frames.append(Data(pending.prefix(VoicePcm.frameBytes)))
            pending.removeFirst(VoicePcm.frameBytes)
        }
        return frames
    }
}
