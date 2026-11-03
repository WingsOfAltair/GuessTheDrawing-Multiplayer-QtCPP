package com.guessthedrawing.mobile

import android.os.Handler
import android.os.Looper
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID
import kotlin.concurrent.thread

class GameClient {
    val community by lazy { CommunityClient(this, { type, fields -> sendMaster(type, fields) }, { notifyChange() }) }
    private var serverTextMuted = false
    private var serverVoiceMuted = false
    private var muteExpiresAt = 0L
    private val muteActive get() = muteExpiresAt == 0L || muteExpiresAt > System.currentTimeMillis() / 1000
    var changed: () -> Unit = {}
    var voiceReceived: (JSONObject) -> Unit = {}
    private val ui = Handler(Looper.getMainLooper())
    private var master: Connection? = null
    private var game: Connection? = null
    private var token = ""
    private var authId = ""
    private var joinId = ""
    private var joinPassword = ""
    private var wantSpectator = false
    private var reconnectUntil = 0L
    private var requestEpoch = 0
    var host = ""; private set
    var port = 45450; private set
    var page = "login"; private set
    var busy = false; private set
    var status = "Connect to your game's master server"; private set
    var profile = JSONObject(); private set
    var rooms = JSONArray(); private set
    var room = JSONObject(); private set
    var players = JSONArray(); private set
    var leaderboards = mutableMapOf<String, JSONArray>()
    var spectator = false; private set
    var queued = false; private set
    var queuePosition = 0; private set
    var roundActive = false; private set
    var starting = false; private set
    var drawer = ""; private set
    var word = ""; private set
    var seconds = 0; private set
    var guessed = false; private set
    var connected = false; private set
    val messages = mutableListOf<String>()
    val scene = CanvasScene()
    val username get() = profile.optString("username")
    val canDraw get() = connected && roundActive && !spectator && drawer == username
    val canChat get() = connected && !canDraw && !guessed
    val canSpeak get() = canChat && !(serverVoiceMuted && muteActive)
    val canTextChat get() = canChat && !(serverTextMuted && muteActive)
    val canGuess get() = canChat && roundActive && !spectator
    val roundLabel get() = when {
        !connected -> "Reconnecting…"
        starting -> "Next round in $seconds s"
        roundActive -> "$drawer is drawing · $seconds s"
        else -> "Waiting for the next round"
    }
    private val pulse = object : Runnable {
        override fun run() {
            if (token.isNotEmpty()) {
                if (master == null) connectMaster()
                else if (page == "lobby") refreshRooms()
                if (connected) sendGame("Ping")
            }
            ui.postDelayed(this, 5000)
        }
    }
    init { ui.postDelayed(pulse, 5000) }
    private fun notifyChange() { changed() }
    fun notice(text: String) { status = text; notifyChange() }
    private fun log(text: String) {
        messages.add(text)
        if (messages.size > 200) messages.removeAt(0)
    }
    private fun id() = UUID.randomUUID().toString()
    private fun waitForReply(text: String) {
        busy = true; status = text
        val epoch = ++requestEpoch
        ui.postDelayed({
            if (epoch == requestEpoch && busy) {
                busy = false; status = "The server did not respond. Please try again."
                if (authId.isNotEmpty()) { authId = ""; master?.close(); master = null }
                if (joinId.isNotEmpty()) leaveRoom(false)
                notifyChange()
            }
        }, 15000)
    }
    fun authenticate(server: String, serverPort: Int, user: String, password: String, name: String, create: Boolean) {
        if (busy) return
        if (server.isBlank() || serverPort !in 1..65535 || user.isBlank() || password.isEmpty() ||
            (create && name.isBlank())) { notice("Enter the server, username, password and display name if registering."); return }
        logout(false)
        host = server.trim(); port = serverPort
        authId = id()
        val request = Protocol.message(if (create) "CreateAccount" else "Login",
            json("requestId" to authId, "username" to user.trim(), "password" to password,
                "displayName" to name.trim()))
        waitForReply(if (create) "Creating account…" else "Signing in…")
        connectMaster(request)
        notifyChange()
    }
    private fun connectMaster(authentication: JSONObject? = null) {
        master?.close()
        lateinit var socket: Connection
        socket = Connection(
            onReady = {
                if (master === socket) {
                    if (authentication != null) socket.send(authentication) else refreshRooms()
                }
            },
            onMessage = { if (master === socket) handleMaster(it) },
            onClosed = {
                if (master === socket) {
                    master = null; busy = false; authId = ""
                    status = if (token.isEmpty()) "Cannot connect: $it" else "Master disconnected; reconnecting…"
                    notifyChange()
                }
            })
        master = socket
        socket.connect(host, port)
    }
    private fun sendMaster(type: String, fields: JSONObject = JSONObject()): Boolean {
        if (!fields.has("requestId")) fields.put("requestId", id())
        fields.put("authToken", token)
        return master?.send(Protocol.message(type, fields)) == true
    }
    fun refreshRooms() { if (token.isNotEmpty()) sendMaster("ListRooms") }
    fun refreshLeaderboards() {
        listOf("guessing", "drawing").forEach { sendMaster("LeaderboardRequest", json("track" to it)) }
    }
    fun saveProfile(name: String, country: String, currentPassword: String, newPassword: String) {
        if (busy) return
        if (sendMaster("UpdateProfile", json("displayName" to name, "countryCode" to country.uppercase(),
                "currentPassword" to currentPassword, "newPassword" to newPassword)))
            waitForReply("Saving profile…")
        else status = "Master is reconnecting. Try again shortly."
        notifyChange()
    }
    private fun handleMaster(m: JSONObject) {
        if (community.handle(m) && m.optString("type") != "Error") return
        when (m.optString("type")) {
            "AuthResult" -> {
                if (m.optString("requestId") != authId || authId.isEmpty()) return
                authId = ""; busy = false
                if (!m.optBoolean("ok")) status = m.optString("error")
                else {
                    token = m.optString("authToken"); profile = m.obj("profile")
                    page = "lobby"; status = "Choose a room"; refreshRooms(); refreshLeaderboards()
                    community.refresh()
                }
            }
            "RoomList" -> { rooms = m.array("rooms"); if (!busy && page == "lobby") status = "Choose a room" }
            "LeaderboardResult" -> leaderboards[m.optString("track")] = m.array("players")
            "ProfileUpdated" -> { profile = m.obj("profile"); busy = false; status = "Profile saved" }
            "SessionReplaced" -> { logout(false); status = "This account signed in on another device." }
            "VersionMismatch" -> versionMismatch(m)
            "Error" -> {
                busy = false; status = m.optString("message")
                if (status.contains("Session", true) || status.contains("expired", true)) {
                    val error = status; logout(false); status = error
                }
            }
            "ModerationNotice" -> moderation(m)
        }
        notifyChange()
    }
    fun join(selected: JSONObject, asSpectator: Boolean, password: String = "") {
        if (busy || token.isEmpty()) return
        room = selected.copy(); wantSpectator = asSpectator; joinPassword = password
        reconnectUntil = 0; messages.clear()
        connectGame()
    }
    private fun connectGame() {
        game?.close(); connected = false
        joinId = id()
        waitForReply("Joining " + room.optString("roomName") + "…")
        lateinit var socket: Connection
        socket = Connection(
            onReady = { if (game === socket) sendJoin() },
            onMessage = { if (game === socket) handleGame(it) },
            onClosed = {
                if (game === socket) {
                    game = null; connected = false; busy = false
                    if (page == "game") {
                        if (reconnectUntil == 0L) reconnectUntil = System.currentTimeMillis() + 25000
                        status = "Connection lost; reconnecting…"
                        val expectedRoom = room.optString("roomId")
                        ui.postDelayed({
                            if (page == "game" && room.optString("roomId") == expectedRoom && game == null) {
                                if (System.currentTimeMillis() < reconnectUntil) connectGame()
                                else { leaveRoom(false); notice("Reconnect timed out. Join the room again.") }
                            }
                        }, 1500)
                    } else status = "Cannot join room: $it"
                    notifyChange()
                }
            })
        game = socket
        val gamePort = room.optInt("port")
        if (gamePort !in 1..65535 || room.optString("host").isBlank()) {
            leaveRoom(false); notice("The room advertises an invalid address."); return
        }
        socket.connect(room.optString("host"), gamePort)
        notifyChange()
    }
    private fun sendJoin(proof: String? = null) {
        val fields = json("requestId" to joinId, "roomId" to room.optString("roomId"),
            "authToken" to token, "spectator" to wantSpectator)
        if (proof != null) fields.put("passkeyProof", proof)
        game?.send(Protocol.message("JoinRoom", fields))
    }
    fun sendGame(type: String, fields: JSONObject = JSONObject()): Boolean =
        game?.send(Protocol.message(type, fields)) == true
    fun sendText(text: String, guess: Boolean) {
        if (text.isBlank() || (guess && !canGuess) || (!guess && !canTextChat)) return
        sendGame(if (guess) "Guess" else "RoomChat", json("text" to text.take(if (guess) 100 else 300)))
    }
    fun changeRole() {
        if (connected && !busy && sendGame("ChangeRole", json("requestId" to id(), "spectator" to (queued || !spectator))))
            waitForReply("Changing role…")
        notifyChange()
    }
    fun operation(op: JSONObject) {
        if (!canDraw) return
        if (sendGame("CanvasOperation", json("operation" to op))) {
            scene.apply(op); notifyChange()
        }
    }
    private fun roster(m: JSONObject) {
        players = m.array("players")
        players.objects().find { it.obj("profile").optString("username") == username }?.let {
            guessed = it.optBoolean("chatBlocked") || it.optBoolean("guessed")
            spectator = it.optBoolean("spectator")
        }
    }
    private fun queue(m: JSONObject) { queued = m.optBoolean("queued"); queuePosition = m.optInt("position") }
    private fun handleGame(m: JSONObject) {
        when (m.optString("type")) {
            "RoomPasskeyChallenge" -> {
                if (m.optString("requestId") != joinId) return
                val password = joinPassword; joinPassword = ""
                if (password.isEmpty()) { leaveRoom(false); notice("Rejoin with the room's passkey."); return }
                val expected = game; val request = joinId
                thread(name = "gtd-passkey", isDaemon = true) {
                    val proof = runCatching { Protocol.passkeyProof(password, m.optString("passkeySalt"), m.optString("passkeyChallenge")) }
                    ui.post {
                        if (expected === game && request == joinId) proof.fold(
                            onSuccess = { sendJoin(it) },
                            onFailure = { leaveRoom(false); notice("Invalid private room challenge.") })
                    }
                }
            }
            "JoinResult" -> {
                if (m.optString("requestId") != joinId) return
                joinId = ""; busy = false; joinPassword = ""
                if (!m.optBoolean("ok")) {
                    val error = m.optString("error"); leaveRoom(false); status = error
                } else {
                    page = "game"; connected = true; reconnectUntil = 0
                    spectator = m.optBoolean("spectator"); wantSpectator = spectator
                    roundActive = m.optBoolean("roundActive"); starting = m.optBoolean("roundStarting")
                    drawer = m.optString("drawerUsername"); word = ""; seconds = m.optInt("secondsRemaining")
                    guessed = false; roster(m); queue(m.obj("queue"))
                    scene.replace(m.array("layers"), m.array("objects"))
                    status = room.optString("roomName"); log("Joined " + status)
                    serverTextMuted = false; serverVoiceMuted = false; community.refreshRoom()
                }
            }
            "CanvasState" -> scene.replace(m.array("layers"), m.array("objects"))
            "CanvasOperation" -> scene.apply(m.obj("operation"))
            "ClearCanvas" -> scene.apply(json("action" to "clear"))
            "RoomRoster" -> roster(m)
            "QueueStatus" -> queue(m)
            "RoleChanged", "RoleChangeResult" -> {
                busy = false
                if (!m.has("ok") || m.optBoolean("ok")) {
                    spectator = m.optBoolean("spectator"); wantSpectator = spectator
                    queue(m.obj("queue")); if (m.has("queued")) queued = m.optBoolean("queued")
                } else log(m.optString("error"))
            }
            "RoundStarted" -> {
                roundActive = true; starting = false; guessed = false
                drawer = m.optString("drawerUsername"); word = ""; seconds = m.optInt("secondsRemaining")
            }
            "WordAssigned" -> word = m.optString("word")
            "Countdown", "IntermissionCountdown" -> seconds = m.optInt("secondsRemaining")
            "RoundStarting" -> {
                roundActive = false; starting = true; drawer = ""; guessed = false; word = ""
                seconds = m.optInt("secondsRemaining")
            }
            "RoundPaused" -> { roundActive = false; starting = false; log(m.optString("message")) }
            "RoundResumed" -> { roundActive = true; starting = false; seconds = m.optInt("secondsRemaining") }
            "RoundEnded" -> {
                roundActive = false; starting = false; guessed = false; drawer = ""
                word = m.optString("word"); log("The word was $word")
            }
            "GuessCorrect" -> {
                if (m.optString("username") == username) guessed = true
                log(m.optString("displayName") + " guessed correctly! +" + m.optInt("points"))
            }
            "ChatMessage" -> log(m.optString("displayName") + ": " + m.optString("text"))
            "VoiceFrame" -> { if (connected) voiceReceived(m); return }
            "VersionMismatch" -> versionMismatch(m)
            "ModerationNotice" -> moderation(m)
            "ModerationAnnouncement" -> {
                if (m.optString("targetUsername") == username && m.optString("action") == "unmute") { serverTextMuted = false; serverVoiceMuted = false }
                log(m.optString("targetUsername") + ": " + m.optString("action") + " · " + m.optString("reason"))
            }
            "ServerUpdateScheduled" -> log("Server update in " + m.optInt("secondsRemaining") + " seconds")
            "ServerRestarting" -> { leaveRoom(false); status = "Server restarting. Refresh rooms shortly." }
            "Error" -> { busy = false; log(m.optString("message")); status = m.optString("message") }
        }
        notifyChange()
    }
    private fun moderation(m: JSONObject) {
        val text = m.optString("reason", m.optString("message", "Server moderation notice"))
        log(text); status = text
        if (m.optString("action") == "mute") {
            serverTextMuted = m.optBoolean("textChat"); serverVoiceMuted = m.optBoolean("voiceChat"); muteExpiresAt = m.optLong("expiresAt")
        }
        if (m.optString("action") == "unmute") { serverTextMuted = false; serverVoiceMuted = false; muteExpiresAt = 0 }
        if (m.optString("action") in listOf("kick", "ban")) { leaveRoom(false); status = text }
    }
    private fun versionMismatch(m: JSONObject) {
        val required = m.optString("requiredVersion")
        logout(false)
        status = "Server requires version $required. Install a matching mobile build."
    }
    fun leaveRoom(send: Boolean = true) {
        if (send) game?.finish(Protocol.message("LeaveRoom")) else game?.close()
        game = null; connected = false; reconnectUntil = 0
        joinPassword = ""; joinId = ""; busy = false; requestEpoch++
        roundActive = false; starting = false; drawer = ""; word = ""; guessed = false; queued = false
        page = if (token.isEmpty()) "login" else "lobby"
        refreshRooms(); notifyChange()
    }
    fun logout(send: Boolean = true) {
        leaveRoom(send)
        if (send) master?.finish(Protocol.message("Logout", json("authToken" to token))) else master?.close()
        master = null; token = ""; authId = ""; profile = JSONObject()
        rooms = JSONArray(); leaderboards.clear(); page = "login"; busy = false
        community.clear()
        notifyChange()
    }
    fun close() { logout(); ui.removeCallbacksAndMessages(null) }
    fun setForeground(active: Boolean) {
        community.foreground = active
        if (token.isNotEmpty()) sendMaster("PresenceStatusUpdate", json("status" to if (active) "active" else "idle"))
    }
}
