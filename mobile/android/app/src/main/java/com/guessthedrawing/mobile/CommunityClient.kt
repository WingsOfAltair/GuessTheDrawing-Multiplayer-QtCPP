package com.guessthedrawing.mobile

import android.os.Handler
import android.os.Looper
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/** Social and moderation requests use the authenticated master connection, including while in a room. */
class CommunityClient(private val client: GameClient, private val send: (String, JSONObject) -> Boolean, private val changed: () -> Unit) {
    var friends = listOf<JSONObject>(); private set
    var requests = listOf<JSONObject>(); private set
    var notifications = listOf<JSONObject>(); private set
    var blocked = listOf<String>(); private set
    var conversations = listOf<JSONObject>(); private set
    var users = listOf<JSONObject>(); private set
    var admin = JSONObject(); private set
    var status = ""; private set
    var query = ""; private set
    var openConversation = ""
    var foreground = true
    val profiles = mutableMapOf<String, JSONObject>()
    val messages = mutableMapOf<String, List<JSONObject>>()
    val hasOlder = mutableMapOf<String, Boolean>()
    val mutedText = mutableSetOf<String>()
    val mutedVoice = mutableSetOf<String>()
    val invitations = mutableListOf<JSONObject>()
    private data class Pending(val type: String, val fields: JSONObject, val key: String)
    private val pending = mutableMapOf<String, Pending>()
    private val latest = mutableMapOf<String, String>()
    private val ui = Handler(Looper.getMainLooper())
    val busy get() = pending.isNotEmpty()
    fun sectionCount(section: String): Int = when (section) {
        "Friends" -> friends.size
        "Requests" -> requests.size + notifications.count { it.optString("kind") == "friend_request_accepted" }
        "Inbox" -> conversations.size
        "Notifications" -> notifications.size + invitations.size
        "Find players" -> users.size
        "Blocked" -> blocked.size
        "Moderation" -> listOf("bans", "mutes", "staff").sumOf { admin.array(it).length() }
        else -> 0
    }
    fun sectionLabel(section: String) = L10n.format("%1 (%2)", L10n.text(section), sectionCount(section))
    val badge get() = requests.count { it.optString("recipientUsername") == client.username && it.optString("status") == "pending" } +
        notifications.size + conversations.sumOf { it.optInt("unreadCount") } + invitations.size
    val ownRole: JSONObject get() = client.players.objects().find { it.obj("profile").optString("username") == client.username }?.obj("profile") ?: client.profile
    val level get() = CommunityPolicy.level(ownRole)
    val canModerate get() = client.connected && level > 0
    fun targetProfile(user: String): JSONObject = client.players.objects().find { it.obj("profile").optString("username") == user }?.obj("profile")
        ?: profiles[user]?.obj("profile") ?: json("username" to user, "displayName" to user)
    fun request(type: String, fields: JSONObject = JSONObject(), key: String = type): Boolean {
        if (pending.values.any { it.key == key && it.fields.toString() == fields.toString() }) return false
        val id = UUID.randomUUID().toString()
        if (!send(type, fields.copy().put("requestId", id))) { status = "Master is reconnecting. Please retry."; changed(); return false }
        pending[id] = Pending(type, fields.copy(), key); latest[key] = id; status = ""
        ui.postDelayed({
            if (pending.remove(id) != null) { status = "Request timed out. Please retry."; changed() }
        }, 15000)
        changed(); return true
    }
    fun refresh() { request("SocialListRequest"); request("InboxListRequest") }
    fun search(value: String) { query = value.trim().take(80); request("UserSearchRequest", json("query" to query)) }
    fun profile(user: String) {
        request("UserProfileRequest", json("targetUsername" to user), "profile:$user")
        if (client.connected) request("PlayerProfileRequest", json("serverId" to client.room.optString("serverId"), "username" to user), "role:$user")
    }
    fun inbox(user: String, older: Boolean = false) {
        val fields = json("targetUsername" to user)
        if (older) messages[user]?.minOfOrNull { it.optLong("id") }?.let { fields.put("beforeId", it) }
        request("InboxRequest", fields, "inbox:$user")
    }
    fun action(type: String, user: String = "", fields: JSONObject = JSONObject()): Boolean {
        val data = fields.copy()
        if (user.isNotBlank()) data.put("targetUsername", user)
        return request(type, data, "action:$type:$user:${data.optLong("friendRequestId")}:${data.optLong("notificationId")}:${data.optLong("messageId")}")
    }
    fun roomRequest(type: String, user: String = "", fields: JSONObject = JSONObject()): Boolean =
        action(type, user, fields.copy().put("serverId", client.room.optString("serverId")))
    fun refreshRoom() {
        mutedText.clear(); mutedVoice.clear(); admin = JSONObject()
        profiles.values.forEach { it.obj("profile").remove("roomRoleLoaded") }
        if (client.connected) roomRequest("PlayerMuteListRequest")
    }
    fun refreshAdmin() { if (canModerate) roomRequest("ServerAdminDataRequest") }
    fun mute(user: String, text: Boolean, voice: Boolean) = roomRequest("PlayerMuteRequest", user, json("textChat" to text, "voiceChat" to voice))
    fun dismissInvitation(item: JSONObject) { invitations.remove(item); changed() }
    fun handle(m: JSONObject): Boolean {
        val type = m.optString("type")
        val id = m.optString("requestId")
        val p = pending.remove(id)
        if (p != null && p.fields.has("serverId") && p.fields.optString("serverId") != client.room.optString("serverId")) { changed(); return true }
        if (p != null && latest[p.key] != id && type in listOf("UserSearchResult", "InboxResult", "UserProfileResult")) { changed(); return true }
        when (type) {
            "SocialListResult" -> {
                friends = m.array("friends").objects().sortedBy { when (it.optString("presenceStatus")) { "offline" -> 2; "idle", "away" -> 1; else -> 0 } }
                requests = m.array("requests").objects(); notifications = m.array("notifications").objects()
                blocked = (0 until m.array("blocked").length()).map { m.array("blocked").optString(it) }
            }
            "UserSearchResult" -> users = m.array("users").objects()
            "UserProfileResult" -> {
                val entry = m.obj("user"); val user = entry.obj("profile").optString("username")
                profiles[user]?.obj("profile")?.let { old ->
                    if (old.has("roomRoleLoaded")) entry.obj("profile").put("serverRole", old.optString("serverRole"))
                        .put("masterSuperAdmin", old.optBoolean("masterSuperAdmin")).put("roomRoleLoaded", true)
                }
                profiles[user] = entry
            }
            "PlayerProfileResult" -> {
                val profile = m.obj("profile").put("roomRoleLoaded", true); val user = profile.optString("username")
                profiles.getOrPut(user) { JSONObject() }.put("profile", profile)
            }
            "InboxListResult" -> conversations = m.array("conversations").objects()
            "InboxResult" -> {
                val user = m.optString("targetUsername"); val batch = m.array("messages").objects()
                messages[user] = ((if ((p?.fields?.optLong("beforeId", 0) ?: 0L) > 0L) messages[user].orEmpty() else emptyList()) + batch)
                    .distinctBy { it.optLong("id") }.sortedBy { it.optLong("id") }
                hasOlder[user] = batch.size >= 100; request("InboxListRequest")
            }
            "DirectMessageResult", "DirectMessageReceived" -> {
                val msg = m.obj("message"); val user = if (msg.optString("senderUsername") == client.username) msg.optString("recipientUsername") else msg.optString("senderUsername")
                messages[user] = (messages[user].orEmpty() + msg).distinctBy { it.optLong("id") }.sortedBy { it.optLong("id") }
                if (foreground && openConversation == user) action("InboxMarkRead", user)
                request("InboxListRequest"); if (type == "DirectMessageResult") status = "Message sent"
            }
            "InboxDeleteResult" -> {
                val user = m.optString("targetUsername")
                if (m.optString("action") == "InboxConversationDelete") { messages.remove(user); if (openConversation == user) openConversation = "" }
                else messages.keys.toList().forEach { key -> messages[key] = messages[key].orEmpty().filter { it.optLong("id") != m.optLong("messageId") } }
                request("InboxListRequest"); status = "Deleted from your inbox"
            }
            "UnreadCountChanged" -> conversations.find { it.optString("username") == m.optString("username") }?.put("unreadCount", m.optInt("unreadCount"))
            "SocialChanged", "SocialActionResult" -> {
                refresh(); if (query.isNotEmpty()) search(query)
                if (type == "SocialActionResult") {
                    profiles.keys.toList().forEach { profile(it) }
                    status = when (m.optString("action")) { "FriendRequestSend" -> "Friend request sent"; else -> "Saved" }
                }
            }
            "PresenceChanged" -> {
                val user = m.optString("username"); val state = m.optString("status")
                (friends + users + listOfNotNull(profiles[user])).filter { it.obj("profile").optString("username") == user }.forEach {
                    it.put("presenceStatus", state).put("hasRoom", m.optBoolean("hasRoom")).put("room", m.obj("room"))
                }
                conversations.find { it.optString("username") == user }?.put("presence", state)
            }
            "RoomInviteReceived" -> { invitations.add(m.copy()); if (invitations.size > 50) invitations.removeAt(0) }
            "RoomInviteResult" -> status = "Room invitation sent"
            "PlayerMuteList" -> { mutedText.clear(); mutedVoice.clear(); m.array("preferences").objects().forEach { updateMute(it) } }
            "PlayerMuteResult" -> { updateMute(m); status = "Mute preference saved" }
            "ServerAdminData" -> if (m.optString("serverId") == client.room.optString("serverId")) admin = m.copy()
            "ModerationResult", "StaffRoleResult" -> { refreshAdmin(); status = "Server settings updated" }
            "PlayerReportResult" -> status = if (m.optBoolean("automaticBanApplied")) "Report received; enforcement applied" else "Report received for moderator review"
            "Error" -> { if (p == null) return false; status = m.optString("message", "Request failed") }
            else -> return false
        }
        changed(); return true
    }
    private fun updateMute(m: JSONObject) {
        val user = m.optString("targetUsername")
        if (m.optBoolean("textChat")) mutedText.add(user) else mutedText.remove(user)
        if (m.optBoolean("voiceChat")) mutedVoice.add(user) else mutedVoice.remove(user)
    }
    fun clear() {
        pending.clear(); latest.clear(); ui.removeCallbacksAndMessages(null)
        friends = emptyList(); requests = emptyList(); notifications = emptyList(); blocked = emptyList(); conversations = emptyList(); users = emptyList()
        profiles.clear(); messages.clear(); hasOlder.clear(); invitations.clear(); mutedText.clear(); mutedVoice.clear()
        admin = JSONObject(); openConversation = ""; status = ""; query = ""
    }
}

object CommunityPolicy {
    fun level(profile: JSONObject) = if (profile.optBoolean("masterSuperAdmin")) 3 else when (profile.optString("serverRole", "player")) { "admin" -> 2; "moderator" -> 1; else -> 0 }
    val durationUnits = linkedMapOf("Seconds" to 1L, "Minutes" to 60L, "Hours" to 3600L, "Days" to 86400L,
        "Weeks" to 604800L, "Months (30 days)" to 2592000L, "Years (365 days)" to 31536000L)
}
