import AVFoundation
import Foundation
import Combine

@MainActor
final class VoiceChat: ObservableObject {
    @Published private(set) var recording = false
    @Published var listening = true { didSet { if !listening { stopPlayback() } } }
    private var engine = AVAudioEngine()
    private var speakers: [String: AVAudioPlayerNode] = [:]
    private var queued: [String: Int] = [:]
    private var lastReceived: [String: Date] = [:]
    private var observers: [NSObjectProtocol] = []
    private var tapInstalled = false
    private var generation = 0
    private var packetizer = VoicePacketizer()
    private var foreground = true
    private var interrupted = false
    private var lastError = Date.distantPast
    private weak var client: GameClient?
    private let wireFormat = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: 16000, channels: 1, interleaved: false)!
    private let outputFormat = AVAudioFormat(standardFormatWithSampleRate: 16000, channels: 1)!
    init() {
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: .main) { [weak self] note in
            let began = (note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt) == AVAudioSession.InterruptionType.began.rawValue
            Task { @MainActor in
                guard let self else { return }
                self.interrupted = began
                if began { self.close(); self.client?.notice("Voice interrupted. Tap Mic to speak when ready.") }
            }
        })
        observers.append(center.addObserver(forName: AVAudioSession.routeChangeNotification, object: nil, queue: .main) { [weak self] note in
            let reason = (note.userInfo?[AVAudioSessionRouteChangeReasonKey] as? UInt) ?? 0
            guard reason != AVAudioSession.RouteChangeReason.categoryChange.rawValue else { return }
            Task { @MainActor in self?.routeChanged() }
        })
        observers.append(center.addObserver(forName: AVAudioSession.mediaServicesWereResetNotification, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.routeChanged() }
        })
        observers.append(center.addObserver(forName: .AVAudioEngineConfigurationChange, object: nil, queue: .main) { [weak self] note in
            let changedEngine = note.object as? AVAudioEngine
            Task { @MainActor in
                guard let self, changedEngine === self.engine, !self.engine.isRunning else { return }
                self.routeChanged()
            }
        })
    }
    deinit { for observer in observers { NotificationCenter.default.removeObserver(observer) } }
    func attach(client: GameClient) { self.client = client; foreground = true }
    func setForeground(_ value: Bool) { foreground = value; if !value { close() } }
    private func routeChanged() {
        let wasRecording = recording
        resetEngine()
        if wasRecording { client?.notice("Audio output changed. Tap Mic to speak again.") }
    }
    func requestMicrophone(client: GameClient) {
        self.client = client
        guard foreground, !interrupted, client.canSpeak else { client.notice("Microphone is unavailable for your current role."); return }
        let requestedGeneration = generation
        AVAudioSession.sharedInstance().requestRecordPermission { [weak self, weak client] granted in
            Task { @MainActor in
                guard let self, let client, self.foreground, !self.interrupted,
                      self.generation == requestedGeneration, client.canSpeak else { return }
                if granted { self.start(client: client) }
                else { client.notice("Microphone access is off. Enable it in Settings to speak.") }
            }
        }
    }
    private func configureSession(record: Bool) throws {
        let session = AVAudioSession.sharedInstance()
        try session.setCategory(record ? .playAndRecord : .playback, mode: record ? .voiceChat : .default,
            options: record ? [.defaultToSpeaker, .allowBluetooth] : [])
        if record { try session.setPreferredIOBufferDuration(0.02) }
        try session.setActive(true)
    }
    private func start(client: GameClient) {
        resetEngine()
        do {
            try configureSession(record: true)
            let input = engine.inputNode
            // Voice processing provides echo cancellation on supported devices/routes.
            try? input.setVoiceProcessingEnabled(true)
            let format = input.outputFormat(forBus: 0)
            guard format.sampleRate > 0, format.channelCount > 0, let converter = AVAudioConverter(from: format, to: wireFormat) else {
                throw NSError(domain: "VoiceChat", code: 1, userInfo: [NSLocalizedDescriptionKey: "Microphone format is unavailable"])
            }
            let expected = generation, targetFormat = wireFormat
            input.installTap(onBus: 0, bufferSize: 1024, format: format) { [weak self, weak client] buffer, _ in
                let capturedAt = Date()
                let capacity = AVAudioFrameCount(ceil(Double(buffer.frameLength) * 16000 / format.sampleRate) + 32)
                guard let output = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: capacity) else { return }
                var supplied = false, error: NSError?
                converter.convert(to: output, error: &error) { _, status in
                    if supplied { status.pointee = .noDataNow; return nil }
                    supplied = true; status.pointee = .haveData; return buffer
                }
                guard error == nil, let samples = output.int16ChannelData?[0], output.frameLength > 0 else { return }
                let data = Data(bytes: samples, count: Int(output.frameLength) * 2)
                Task { @MainActor in
                    guard let self, let client, self.foreground, self.recording, self.generation == expected, client.canSpeak else { return }
                    guard Date().timeIntervalSince(capturedAt) < 0.2 else { self.packetizer = VoicePacketizer(); return }
                    for frame in self.packetizer.append(data) {
                        client.sendGame("VoiceFrame", ["sampleRate": VoicePcm.sampleRate, "audio": frame.base64EncodedString()])
                    }
                }
            }
            tapInstalled = true; engine.prepare(); try engine.start(); recording = true
        } catch { resetEngine(); client.notice("Could not start microphone: " + error.localizedDescription) }
    }
    private func resetEngine() {
        generation += 1; packetizer = VoicePacketizer(); engine.stop()
        if tapInstalled { engine.inputNode.removeTap(onBus: 0); tapInstalled = false }
        recording = false
        for player in speakers.values { player.stop(); engine.detach(player) }
        speakers = [:]; queued = [:]; lastReceived = [:]; engine = AVAudioEngine()
    }
    func stopRecording() {
        if recording || tapInstalled {
            resetEngine()
            try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
        }
        else { generation += 1 }
    }
    func receive(_ message: JSON) {
        guard foreground, !interrupted, listening, client?.connected == true,
              message.int("sampleRate") == VoicePcm.sampleRate, let bytes = VoicePcm.decode(message.string("audio")) else { return }
        let user = message.string("username")
        guard !user.isEmpty, user != client?.username, speakers[user] != nil || speakers.count < 32 else { return }
        do {
            if !engine.isRunning { try configureSession(record: recording) }
            let player: AVAudioPlayerNode
            if let existing = speakers[user] { player = existing }
            else { player = AVAudioPlayerNode(); speakers[user] = player; engine.attach(player); engine.connect(player, to: engine.mainMixerNode, format: outputFormat) }
            // Drop stale queued speech rather than building seconds of latency.
            if queued[user, default: 0] >= 12 { return }
            guard let buffer = AVAudioPCMBuffer(pcmFormat: outputFormat, frameCapacity: 320), let samples = buffer.floatChannelData?[0] else { return }
            buffer.frameLength = 320
            let raw = [UInt8](bytes)
            for i in 0..<320 { samples[i] = Float(Int16(bitPattern: UInt16(raw[i*2]) | UInt16(raw[i*2+1]) << 8)) / 32768 }
            if !engine.isRunning { engine.prepare(); try engine.start() }
            lastReceived[user] = Date()
            queued[user, default: 0] += 1
            player.scheduleBuffer(buffer, completionCallbackType: .dataPlayedBack) { [weak self, weak player] _ in
                Task { @MainActor in
                    guard let self, let player, self.speakers[user] === player else { return }
                    self.queued[user] = max(0, self.queued[user, default: 0] - 1)
                    if self.queued[user] == 0 { self.retireIdleSpeaker(user, player: player) }
                }
            }
            if !player.isPlaying { player.play() }
        } catch {
            resetEngine()
            if Date().timeIntervalSince(lastError) > 5 { lastError = Date(); client?.notice("Room audio: " + error.localizedDescription) }
        }
    }
    private func retireIdleSpeaker(_ user: String, player: AVAudioPlayerNode) {
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self, weak player] in
            guard let self, let player, self.speakers[user] === player,
                  self.queued[user, default: 0] == 0,
                  Date().timeIntervalSince(self.lastReceived[user] ?? .distantPast) >= 3 else { return }
            player.stop(); self.engine.detach(player)
            self.speakers.removeValue(forKey: user); self.queued.removeValue(forKey: user); self.lastReceived.removeValue(forKey: user)
            if !self.recording && self.speakers.isEmpty {
                self.engine.stop()
                try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
            }
        }
    }
    func stopPlayback() {
        for player in speakers.values { player.stop(); engine.detach(player) }
        speakers = [:]; queued = [:]; lastReceived = [:]
        if !recording {
            engine.stop()
            try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
        }
    }
    func close() {
        resetEngine()
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }
}
