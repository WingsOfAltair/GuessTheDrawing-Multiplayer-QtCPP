import Foundation
import Network

final class Connection {
    private let queue = DispatchQueue(label: "gtd.tcp")
    private var connection: NWConnection?
    private var decoder = FrameDecoder()
    private var queuedBytes = 0
    private var finished = false
    var onReady: () -> Void = {}
    var onMessage: (JSON) -> Void = { _ in }
    var onClosed: (String) -> Void = { _ in }
    func connect(host: String, port: Int) {
        guard let endpointPort = NWEndpoint.Port(rawValue: UInt16(clamping: port)) else {
            onClosed("Invalid server port"); return
        }
        let parameters = NWParameters.tcp
        if let tcp = parameters.defaultProtocolStack.transportProtocol as? NWProtocolTCP.Options {
            tcp.noDelay = true; tcp.enableKeepalive = true; tcp.keepaliveIdle = 15
        }
        let connection = NWConnection(host: NWEndpoint.Host(host), port: endpointPort, using: parameters)
        self.connection = connection
        connection.stateUpdateHandler = { [weak self] state in
            guard let self, !self.finished else { return }
            switch state {
            case .ready:
                DispatchQueue.main.async { [weak self] in self?.onReady() }
                self.receive()
            case .failed(let error): self.fail(error.localizedDescription)
            case .waiting(let error): self.fail(error.localizedDescription)
            default: break
            }
        }
        connection.start(queue: queue)
        queue.asyncAfter(deadline: .now() + 8) { [weak self] in
            guard let self, !self.finished, self.connection?.state != .ready else { return }
            self.fail("Connection timed out")
        }
    }
    func send(_ message: JSON, closeAfter: Bool = false) {
        guard let data = try? Wire.encode(message) else { return }
        queue.async { [self] in
            guard !self.finished, let connection = self.connection else { return }
            guard self.queuedBytes + data.count <= 2 * Wire.maxFrame else { self.fail("Connection is too slow"); return }
            self.queuedBytes += data.count
            if closeAfter {
                self.queue.asyncAfter(deadline: .now() + 2) { [self] in
                    self.finished = true
                    self.connection?.cancel()
                }
            }
            connection.send(content: data, completion: .contentProcessed { [self] error in
                self.queuedBytes -= data.count
                if let error { self.fail(error.localizedDescription) }
                else if closeAfter { self.finished = true; connection.cancel() }
            })
        }
    }
    private func receive() {
        connection?.receive(minimumIncompleteLength: 1, maximumLength: 8192) { [weak self] data, _, complete, error in
            guard let self, !self.finished else { return }
            do {
                if let data {
                    let frames = try self.decoder.feed(data)
                    DispatchQueue.main.async { [weak self] in
                        guard let self else { return }
                        frames.forEach(self.onMessage)
                    }
                }
                if let error { self.fail(error.localizedDescription) }
                else if complete { self.fail("Connection closed") }
                else { self.receive() }
            } catch { self.fail(error.localizedDescription) }
        }
    }
    private func fail(_ reason: String) {
        guard !finished else { return }
        finished = true; connection?.cancel()
        DispatchQueue.main.async { [weak self] in self?.onClosed(reason) }
    }
    func close() {
        queue.async { [self] in finished = true; connection?.cancel() }
    }
    deinit { connection?.cancel() }
}
