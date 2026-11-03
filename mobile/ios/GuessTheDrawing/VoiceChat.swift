import AVFoundation
import Foundation

@MainActor
final class VoiceChat: ObservableObject {
    @Published private(set) var recording = false
    @Published var listening = true
    private let engine = AVAudioEngine()
    private var speakers: [String: AVAudioPlayerNode] = [:]
    private var queued: [String: Int] = [:]
    private let wireFormat = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: 16000, channels: 1, interleaved: false)!
    private var captureGeneration = 0
    private var captureData = Data()
    func requestMicrophone(client: GameClient) {
        guard client.canChat else { client.notice("Drawing and correctly guessing players can only listen this round."); return }
        AVAudioSession.sharedInstance().requestRecordPermission { [weak self, weak client] granted in
            Task { @MainActor in
                guard let self, let client, client.canChat else { return }
                if granted { self.start(client: client) }
                else { client.notice("Microphone access is off. Enable it in Settings to speak.") }
            }
        }
    }
    private func configureSession(record: Bool) throws {
        let session = AVAudioSession.sharedInstance()
        try session.setCategory(record ? .playAndRecord : .playback, mode: record ? .voiceChat : .default,
            options: record ? [.defaultToSpeaker, .allowBluetooth] : [])
        try session.setActive(true)
    }
    private func start(client: GameClient) {
        stopRecording()
        do {
            // Rebuild output nodes after changing the audio-session category.
            stopPlayback()
            try configureSession(record: true)
            let input = engine.inputNode
            let format = input.outputFormat(forBus: 0)
            guard format.sampleRate > 0, let converter = AVAudioConverter(from: format, to: wireFormat) else {
                client.notice("Microphone format is unavailable."); return
            }
            captureGeneration += 1
            let generation = captureGeneration
            let targetFormat = wireFormat
            input.installTap(onBus: 0, bufferSize: 1024, format: format) { [weak self, weak client] buffer, _ in
                let capacity = AVAudioFrameCount(ceil(Double(buffer.frameLength) * 16000 / format.sampleRate) + 8)
                guard let output = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: capacity) else { return }
                var supplied = false
                var error: NSError?
                converter.convert(to: output, error: &error) { _, status in
                    if supplied { status.pointee = .noDataNow; return nil }
                    supplied = true; status.pointee = .haveData; return buffer
                }
                guard error == nil, let samples = output.int16ChannelData?[0], output.frameLength > 0 else { return }
                let data = Data(bytes: samples, count: Int(output.frameLength) * 2)
                Task { @MainActor in
                    guard let self, let client, self.recording, self.captureGeneration == generation, client.canChat else { return }
                    self.captureData.append(data)
                    while self.captureData.count >= 640 {
                        let frame = self.captureData.prefix(640); self.captureData.removeFirst(640)
                        client.sendGame("VoiceFrame", ["sampleRate": 16000, "audio": frame.base64EncodedString()])
                    }
                }
            }
            recording = true
            try engine.start()
        } catch { stopRecording(); client.notice("Could not start microphone: " + error.localizedDescription) }
    }
    func stopRecording() {
        captureGeneration += 1; captureData.removeAll()
        if recording { engine.inputNode.removeTap(onBus: 0) }
        recording = false
    }
    func receive(_ message: JSON) {
        guard listening, message.int("sampleRate") == 16000,
              message.string("audio").count <= 16384,
              let bytes = Data(base64Encoded: message.string("audio")),
              !bytes.isEmpty, bytes.count <= 8192, bytes.count % 2 == 0 else { return }
        let user = message.string("username")
        guard !user.isEmpty, speakers[user] != nil || speakers.count < 32, queued[user, default: 0] < 16 else { return }
        do {
            if !engine.isRunning { try configureSession(record: recording) }
            let player: AVAudioPlayerNode
            if let existing = speakers[user] { player = existing }
            else {
                player = AVAudioPlayerNode(); speakers[user] = player
                engine.attach(player)
                // The mixer accepts float PCM reliably across device routes.
                let format = AVAudioFormat(standardFormatWithSampleRate: 16000, channels: 1)!
                engine.connect(player, to: engine.mainMixerNode, format: format)
            }
            if !engine.isRunning { try engine.start() }
            let format = AVAudioFormat(standardFormatWithSampleRate: 16000, channels: 1)!
            guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(bytes.count / 2)),
                  let samples = buffer.floatChannelData?[0] else { return }
            buffer.frameLength = buffer.frameCapacity
            let raw = [UInt8](bytes)
            for i in 0..<Int(buffer.frameLength) {
                let value = Int16(bitPattern: UInt16(raw[i * 2]) | UInt16(raw[i * 2 + 1]) << 8)
                samples[i] = Float(value) / 32768
            }
            queued[user, default: 0] += 1
            player.scheduleBuffer(buffer, completionCallbackType: .dataPlayedBack) { [weak self, weak player] _ in
                Task { @MainActor in
                    guard let self, let player, self.speakers[user] === player else { return }
                    self.queued[user] = max(0, self.queued[user, default: 0] - 1)
                }
            }
            if !player.isPlaying { player.play() }
        } catch { stopPlayback() }
    }
    func stopPlayback() {
        for player in speakers.values { player.stop(); engine.detach(player) }
        speakers = [:]; queued = [:]
        if !recording { engine.stop() }
    }
    func close() {
        stopRecording(); stopPlayback(); engine.stop()
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }
}
