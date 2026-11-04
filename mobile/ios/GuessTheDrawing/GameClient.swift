import Foundation
import Combine

@MainActor
final class GameClient: ObservableObject {
    lazy var community = CommunityClient(client: self)
    private var serverTextMuted = false, serverVoiceMuted = false
    private var muteExpiresAt = 0.0
    private var muteActive: Bool { muteExpiresAt == 0 || muteExpiresAt > Date().timeIntervalSince1970 }
    private var master: Connection?
    private var game: Connection?
    private var token = ""
    private var authID = ""
    private var joinID = ""
    private var passkey = ""
    private var wantSpectator = false
    private var reconnectUntil: Date?
    private var epoch = 0
    private var timer: Timer?
    private(set) var host = ""
    private(set) var port = 45450
    private(set) var page = "login"
    private(set) var busy = false
    private(set) var status = "Connect to your game's master server" { didSet { literalStatus = false } }
    private var literalStatus = false
    var displayStatus: String { literalStatus || status == room.string("roomName") ? status : L(status) }
    private(set) var profile: JSON = [:]
    private(set) var rooms: [JSON] = []
    private(set) var room: JSON = [:]
    private(set) var players: [JSON] = []
    private(set) var leaderboards: [String: [JSON]] = [:]
    private(set) var spectator = false
    private(set) var queued = false
    private(set) var queuePosition = 0
    private(set) var roundActive = false
    private(set) var starting = false
    private(set) var drawer = ""
    private(set) var word = ""
    private(set) var seconds = 0
    private(set) var guessed = false
    private(set) var connected = false
    private(set) var messages: [String] = []
    private var literalMessages: Set<String> = []
    func messageText(_ value: String) -> String { literalMessages.contains(value) ? value : L(value) }
    private(set) var scene = CanvasScene()
    var voiceReceived: (JSON) -> Void = { _ in }
    var username: String { profile.string("username") }
    var canDraw: Bool { connected && roundActive && !spectator && drawer == username }
    var canChat: Bool { connected && !canDraw && !guessed }
    var canSpeak: Bool { canChat && !(serverVoiceMuted && muteActive) }
    var canTextChat: Bool { canChat && !(serverTextMuted && muteActive) }
    var canGuess: Bool { canChat && roundActive && !spectator }
    var roundLabel: String {
        if !connected { return "Reconnecting…" }
        if starting { return "Next round in \(seconds) s" }
        if roundActive { return "\(drawer) is drawing · \(seconds) s" }
        return "Waiting for the next round"
    }
    init() {
        timer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self, !self.token.isEmpty else { return }
                if self.master == nil { self.connectMaster() }
                else if self.page == "lobby" { self.refreshRooms() }
                if self.connected { self.sendGame("Ping") }
            }
        }
    }
    private func update() { objectWillChange.send() }
    func communityChanged() { update() }
    func notice(_ text: String) { status = text; update() }
    private func log(_ text: String, literal: Bool = false) {
        messages.append(text); if literal { literalMessages.insert(text) }
        if messages.count > 200 { let removed = messages.removeFirst(); if !messages.contains(removed) { literalMessages.remove(removed) } }
    }
    private func waitForReply(_ text: String) {
        busy = true; status = text; epoch += 1
        let expected = epoch
        DispatchQueue.main.asyncAfter(deadline: .now() + 15) { [weak self] in
            guard let self, self.epoch == expected, self.busy else { return }
            self.busy = false; self.status = "The server did not respond. Please try again."
            if !self.authID.isEmpty { self.authID = ""; self.master?.close(); self.master = nil }
            if !self.joinID.isEmpty { self.leaveRoom(send: false) }
            self.update()
        }
    }
    func authenticate(host: String, port: Int, username: String, password: String, name: String, create: Bool) {
        guard !busy else { return }
        guard !host.trimmingCharacters(in: .whitespaces).isEmpty, (1...65535).contains(port),
              !username.isEmpty, !password.isEmpty, !create || !name.isEmpty else {
            notice("Enter the server, username, password and a display name if registering."); return
        }
        logout(send: false)
        self.host = host.trimmingCharacters(in: .whitespaces); self.port = port; authID = UUID().uuidString
        let request = Wire.message(create ? "CreateAccount" : "Login", [
            "requestId": authID, "username": username.trimmingCharacters(in: .whitespaces),
            "password": password, "displayName": name])
        waitForReply(create ? "Creating account…" : "Signing in…")
        connectMaster(authentication: request); update()
    }
    private func connectMaster(authentication: JSON? = nil) {
        master?.close()
        let socket = Connection()
        socket.onReady = { [weak self, weak socket] in
            guard let self, let socket, self.master === socket else { return }
            if let authentication { socket.send(authentication) } else { self.refreshRooms() }
        }
        socket.onMessage = { [weak self, weak socket] message in
            guard let self, let socket, self.master === socket else { return }
            self.handleMaster(message)
        }
        socket.onClosed = { [weak self, weak socket] error in
            guard let self, let socket, self.master === socket else { return }
            self.master = nil; self.busy = false; self.authID = ""
            self.status = self.token.isEmpty ? "Cannot connect: \(error)" : "Master disconnected; reconnecting…"
            self.update()
        }
        master = socket; socket.connect(host: host, port: port)
    }
    @discardableResult func sendMaster(_ type: String, _ fields: JSON = [:]) -> Bool {
        guard let master else { return false }
        var fields = fields; fields["authToken"] = token
        if fields["requestId"] == nil { fields["requestId"] = UUID().uuidString }
        master.send(Wire.message(type, fields)); return true
    }
    func refreshRooms() { if !token.isEmpty { sendMaster("ListRooms") } }
    func refreshLeaderboards() {
        for track in ["guessing", "drawing"] { sendMaster("LeaderboardRequest", ["track": track]) }
    }
    func saveProfile(name: String, country: String, currentPassword: String, newPassword: String) {
        guard !busy else { return }
        if sendMaster("UpdateProfile", ["displayName": name, "countryCode": country.uppercased(),
            "currentPassword": currentPassword, "newPassword": newPassword]) { waitForReply("Saving profile…") }
        else { status = "Master is reconnecting. Try again shortly." }
        update()
    }
    private func handleMaster(_ m: JSON) {
        if community.handle(m), m.string("type") != "Error" { return }
        switch m.string("type") {
        case "AuthResult":
            guard !authID.isEmpty, m.string("requestId") == authID else { return }
            authID = ""; busy = false
            if !m.bool("ok") { status = m.string("error") }
            else {
                token = m.string("authToken"); profile = m.object("profile"); page = "lobby"
                status = "Choose a room"; refreshRooms(); refreshLeaderboards()
                community.refresh()
            }
        case "RoomList": rooms = m.objects("rooms"); if !busy && page == "lobby" { status = "Choose a room" }
        case "LeaderboardResult": leaderboards[m.string("track")] = m.objects("players")
        case "ProfileUpdated": profile = m.object("profile"); busy = false; status = "Profile saved"
        case "SessionReplaced": logout(send: false); status = "This account signed in on another device."
        case "VersionMismatch": versionMismatch(m)
        case "Error":
            busy = false; status = m.string("message")
            if status.localizedCaseInsensitiveContains("session") || status.localizedCaseInsensitiveContains("expired") {
                let text = status; logout(send: false); status = text
            }
        case "ModerationNotice": moderation(m)
        default: break
        }
        update()
    }
    func join(_ room: JSON, spectator: Bool, passkey: String = "") {
        guard !busy, !token.isEmpty else { return }
        self.room = room; wantSpectator = spectator; self.passkey = passkey; reconnectUntil = nil
        messages = []; literalMessages = []; connectGame()
    }
    private func connectGame() {
        game?.close(); connected = false; joinID = UUID().uuidString
        waitForReply("Joining " + room.string("roomName") + "…")
        guard (1...65535).contains(room.int("port")), !room.string("host").isEmpty else {
            leaveRoom(send: false); notice("The room advertises an invalid address."); return
        }
        let socket = Connection()
        socket.onReady = { [weak self, weak socket] in
            guard let self, let socket, self.game === socket else { return }; self.sendJoin()
        }
        socket.onMessage = { [weak self, weak socket] message in
            guard let self, let socket, self.game === socket else { return }; self.handleGame(message)
        }
        socket.onClosed = { [weak self, weak socket] error in
            guard let self, let socket, self.game === socket else { return }
            self.game = nil; self.connected = false; self.busy = false
            if self.page == "game" {
                if self.reconnectUntil == nil { self.reconnectUntil = Date().addingTimeInterval(25) }
                self.status = "Connection lost; reconnecting…"
                let expected = self.room.string("roomId")
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
                    guard let self, self.page == "game", self.room.string("roomId") == expected, self.game == nil else { return }
                    if let until = self.reconnectUntil, Date() < until { self.connectGame() }
                    else { self.leaveRoom(send: false); self.notice("Reconnect timed out. Join the room again.") }
                }
            } else { self.status = "Cannot join room: " + error }
            self.update()
        }
        game = socket; socket.connect(host: room.string("host"), port: room.int("port")); update()
    }
    private func sendJoin(proof: String? = nil) {
        var fields: JSON = ["requestId": joinID, "roomId": room.string("roomId"),
                            "authToken": token, "spectator": wantSpectator]
        if let proof { fields["passkeyProof"] = proof }
        game?.send(Wire.message("JoinRoom", fields))
    }
    func sendGame(_ type: String, _ fields: JSON = [:]) { game?.send(Wire.message(type, fields)) }
    func sendText(_ text: String, guess: Bool) {
        guard !text.trimmingCharacters(in: .whitespaces).isEmpty, guess ? canGuess : canTextChat else { return }
        sendGame(guess ? "Guess" : "RoomChat", ["text": String(text.prefix(guess ? 100 : 300))])
    }
    func changeRole() {
        guard connected, !busy else { return }
        sendGame("ChangeRole", ["requestId": UUID().uuidString, "spectator": queued || !spectator])
        waitForReply("Changing role…"); update()
    }
    func operation(_ op: JSON) {
        guard canDraw else { return }
        sendGame("CanvasOperation", ["operation": op]); scene.apply(op); update()
    }
    private func roster(_ m: JSON) {
        players = m.objects("players")
        if let own = players.first(where: { $0.object("profile").string("username") == username }) {
            spectator = own.bool("spectator"); guessed = own.bool("chatBlocked") || own.bool("guessed")
        }
    }
    private func queue(_ m: JSON) { queued = m.bool("queued"); queuePosition = m.int("position") }
    private func handleGame(_ m: JSON) {
        switch m.string("type") {
        case "RoomPasskeyChallenge":
            guard m.string("requestId") == joinID else { return }
            let password = passkey; passkey = ""
            guard !password.isEmpty else { leaveRoom(send: false); notice("Rejoin with the room's passkey."); return }
            let request = joinID; let expected = game
            DispatchQueue.global(qos: .userInitiated).async { [weak self] in
                let proof = try? Wire.passkeyProof(password: password, salt: m.string("passkeySalt"), nonce: m.string("passkeyChallenge"))
                DispatchQueue.main.async {
                    guard let self, self.game === expected, self.joinID == request else { return }
                    if let proof { self.sendJoin(proof: proof) }
                    else { self.leaveRoom(send: false); self.notice("Invalid private room challenge.") }
                }
            }
        case "JoinResult":
            guard m.string("requestId") == joinID else { return }
            joinID = ""; busy = false; passkey = ""
            if !m.bool("ok") { let error = m.string("error"); leaveRoom(send: false); status = error }
            else {
                page = "game"; connected = true; reconnectUntil = nil
                spectator = m.bool("spectator"); wantSpectator = spectator
                roundActive = m.bool("roundActive"); starting = m.bool("roundStarting")
                drawer = m.string("drawerUsername"); word = ""; seconds = m.int("secondsRemaining")
                guessed = false; roster(m); queue(m.object("queue"))
                scene.replace(layers: m.objects("layers"), objects: m.objects("objects"))
                status = room.string("roomName"); log("Joined " + status)
                serverTextMuted = false; serverVoiceMuted = false; community.refreshRoom()
            }
        case "CanvasState": scene.replace(layers: m.objects("layers"), objects: m.objects("objects"))
        case "CanvasOperation": scene.apply(m.object("operation"))
        case "ClearCanvas": scene.apply(["action": "clear"])
        case "RoomRoster": roster(m)
        case "QueueStatus": queue(m)
        case "RoleChanged", "RoleChangeResult":
            busy = false
            if m["ok"] == nil || m.bool("ok") {
                spectator = m.bool("spectator"); wantSpectator = spectator; queue(m.object("queue"))
                if m["queued"] != nil { queued = m.bool("queued") }
            } else { log(m.string("error")) }
        case "RoundStarted":
            roundActive = true; starting = false; guessed = false
            drawer = m.string("drawerUsername"); word = ""; seconds = m.int("secondsRemaining")
        case "WordAssigned": word = m.string("word")
        case "Countdown", "IntermissionCountdown": seconds = m.int("secondsRemaining")
        case "RoundStarting":
            roundActive = false; starting = true; drawer = ""; guessed = false; word = ""; seconds = m.int("secondsRemaining")
        case "RoundPaused": roundActive = false; starting = false; log(m.string("message"))
        case "RoundResumed": roundActive = true; starting = false; seconds = m.int("secondsRemaining")
        case "RoundEnded":
            roundActive = false; starting = false; guessed = false; drawer = ""; word = m.string("word")
            log("The word was " + word)
        case "GuessCorrect":
            if m.string("username") == username { guessed = true }
            log(m.string("displayName") + " guessed correctly! +\(m.int("points"))")
        case "ChatMessage": log(L10n.isolate(m.string("displayName")) + ": " + m.string("text"), literal: true)
        case "VoiceFrame": if connected { voiceReceived(m) }; return
        case "VersionMismatch": versionMismatch(m)
        case "ModerationNotice": moderation(m)
        case "ModerationAnnouncement":
            if m.string("targetUsername") == username, m.string("action") == "unmute" { serverTextMuted = false; serverVoiceMuted = false }
            log(L10n.isolate(m.string("targetUsername")) + ": " + L(m.string("action")) + " · " + m.string("reason"), literal: true)
        case "ServerUpdateScheduled": log("Server update in \(m.int("secondsRemaining")) seconds")
        case "ServerRestarting": leaveRoom(send: false); status = "Server restarting. Refresh rooms shortly."
        case "Error": busy = false; status = m.string("message"); log(status)
        default: break
        }
        update()
    }
    private func moderation(_ m: JSON) {
        let text = m.string("reason", m.string("message", "Server moderation notice"))
        log(text, literal: true); status = text; literalStatus = true
        if m.string("action") == "mute" { serverTextMuted = m.bool("textChat"); serverVoiceMuted = m.bool("voiceChat"); muteExpiresAt = m.number("expiresAt") }
        if m.string("action") == "unmute" { serverTextMuted = false; serverVoiceMuted = false; muteExpiresAt = 0 }
        if ["kick", "ban"].contains(m.string("action")) { leaveRoom(send: false); status = text; literalStatus = true }
    }
    private func versionMismatch(_ m: JSON) {
        logout(send: false); status = "Server requires version \(m.string("requiredVersion")). Install a matching mobile build."
    }
    func leaveRoom(send: Bool = true) {
        if send { game?.send(Wire.message("LeaveRoom"), closeAfter: true) } else { game?.close() }
        game = nil; connected = false; reconnectUntil = nil; passkey = ""; joinID = ""; busy = false; epoch += 1
        roundActive = false; starting = false; drawer = ""; word = ""; guessed = false; queued = false
        page = token.isEmpty ? "login" : "lobby"; refreshRooms(); update()
    }
    func logout(send: Bool = true) {
        leaveRoom(send: send)
        if send { master?.send(Wire.message("Logout", ["authToken": token]), closeAfter: true) } else { master?.close() }
        master = nil; token = ""; authID = ""; profile = [:]; rooms = []; leaderboards = [:]; page = "login"; busy = false; community.clear(); update()
    }
    func setForeground(_ active: Bool) {
        community.foreground = active
        if !token.isEmpty { sendMaster("PresenceStatusUpdate", ["status": active ? "active" : "idle"]) }
    }
    deinit { timer?.invalidate(); master?.close(); game?.close() }
}
