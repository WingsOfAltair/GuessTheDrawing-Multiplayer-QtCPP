import Foundation
import Combine

@MainActor
final class CommunityClient: ObservableObject {
    private weak var client: GameClient?
    private(set) var friends: [JSON] = [], requests: [JSON] = [], notifications: [JSON] = [], conversations: [JSON] = [], users: [JSON] = []
    private(set) var blocked: [String] = []
    private(set) var admin: JSON = [:], profiles: [String: JSON] = [:]
    private(set) var messages: [String: [JSON]] = [:], hasOlder: [String: Bool] = [:]
    private(set) var mutedText = Set<String>(), mutedVoice = Set<String>()
    private(set) var invitations: [JSON] = []
    private(set) var status = "", query = ""
    var openConversation = "", foreground = true
    private struct Pending { let type: String; let fields: JSON; let key: String }
    private var pending: [String: Pending] = [:], latest: [String: String] = [:]
    init(client: GameClient) { self.client = client }
    private func update() { objectWillChange.send(); client?.communityChanged() }
    var busy: Bool { !pending.isEmpty }
    var badge: Int {
        requests.filter { $0.string("recipientUsername") == client?.username && $0.string("status") == "pending" }.count +
            notifications.count + conversations.reduce(0) { $0 + $1.int("unreadCount") } + invitations.count
    }
    var ownRole: JSON { client?.players.first { $0.object("profile").string("username") == client?.username }?.object("profile") ?? client?.profile ?? [:] }
    var level: Int { CommunityPolicy.level(ownRole) }
    var canModerate: Bool { client?.connected == true && level > 0 }
    func targetProfile(_ user: String) -> JSON {
        client?.players.first { $0.object("profile").string("username") == user }?.object("profile") ?? profiles[user]?.object("profile") ?? ["username": user, "displayName": user]
    }
    @discardableResult func request(_ type: String, _ fields: JSON = [:], key: String? = nil) -> Bool {
        let key = key ?? type
        if pending.values.contains(where: { $0.key == key && NSDictionary(dictionary: $0.fields).isEqual(to: fields) }) { return false }
        let id = UUID().uuidString
        var payload = fields; payload["requestId"] = id
        guard client?.sendMaster(type, payload) == true else { status = "Master is reconnecting. Please retry."; update(); return false }
        pending[id] = Pending(type: type, fields: fields, key: key); latest[key] = id; status = ""
        DispatchQueue.main.asyncAfter(deadline: .now() + 15) { [weak self] in
            guard let self, self.pending.removeValue(forKey: id) != nil else { return }
            self.status = "Request timed out. Please retry."; self.update()
        }
        update(); return true
    }
    func refresh() { request("SocialListRequest"); request("InboxListRequest") }
    func search(_ value: String) { query = String(value.trimmingCharacters(in: .whitespaces).prefix(80)); request("UserSearchRequest", ["query": query]) }
    func profile(_ user: String) {
        request("UserProfileRequest", ["targetUsername": user], key: "profile:\(user)")
        if client?.connected == true { request("PlayerProfileRequest", ["serverId": client?.room.string("serverId") ?? "", "username": user], key: "role:\(user)") }
    }
    func inbox(_ user: String, older: Bool = false) {
        var fields: JSON = ["targetUsername": user]
        if older, let first = messages[user]?.map({ $0.int("id") }).min() { fields["beforeId"] = first }
        request("InboxRequest", fields, key: "inbox:\(user)")
    }
    @discardableResult func action(_ type: String, user: String = "", fields: JSON = [:]) -> Bool {
        var data = fields; if !user.isEmpty { data["targetUsername"] = user }
        return request(type, data, key: "action:\(type):\(user):\(data.int("friendRequestId")):\(data.int("notificationId")):\(data.int("messageId"))")
    }
    @discardableResult func roomRequest(_ type: String, user: String = "", fields: JSON = [:]) -> Bool {
        var data = fields; data["serverId"] = client?.room.string("serverId") ?? ""
        return action(type, user: user, fields: data)
    }
    func refreshRoom() {
        mutedText = []; mutedVoice = []; admin = [:]
        for user in Array(profiles.keys) {
            var entry = profiles[user] ?? [:], profile = entry.object("profile")
            profile.removeValue(forKey: "roomRoleLoaded"); entry["profile"] = profile; profiles[user] = entry
        }
        if client?.connected == true { roomRequest("PlayerMuteListRequest") }
    }
    func refreshAdmin() { if canModerate { roomRequest("ServerAdminDataRequest") } }
    func mute(_ user: String, text: Bool, voice: Bool) { roomRequest("PlayerMuteRequest", user: user, fields: ["textChat": text, "voiceChat": voice]) }
    func dismissInvitation(_ index: Int) { if invitations.indices.contains(index) { invitations.remove(at: index); update() } }
    @discardableResult func handle(_ m: JSON) -> Bool {
        let type = m.string("type"), id = m.string("requestId"), p = pending.removeValue(forKey: m.string("requestId"))
        if let p, p.fields["serverId"] != nil, p.fields.string("serverId") != client?.room.string("serverId") { update(); return true }
        if let p, latest[p.key] != id, ["UserSearchResult", "InboxResult", "UserProfileResult"].contains(type) { update(); return true }
        switch type {
        case "SocialListResult":
            func rank(_ entry: JSON) -> Int { entry.string("presenceStatus") == "offline" ? 2 : ["idle","away"].contains(entry.string("presenceStatus")) ? 1 : 0 }
            friends = m.objects("friends").sorted { rank($0) < rank($1) }; requests = m.objects("requests"); notifications = m.objects("notifications"); blocked = m["blocked"] as? [String] ?? []
        case "UserSearchResult": users = m.objects("users")
        case "UserProfileResult":
            var entry = m.object("user"), profile = entry.object("profile")
            let user = profile.string("username")
            if let old = profiles[user]?.object("profile"), old.bool("roomRoleLoaded") {
                profile["serverRole"] = old.string("serverRole"); profile["masterSuperAdmin"] = old.bool("masterSuperAdmin"); profile["roomRoleLoaded"] = true
            }
            entry["profile"] = profile; profiles[user] = entry
        case "PlayerProfileResult":
            var profile = m.object("profile"); profile["roomRoleLoaded"] = true
            profiles[profile.string("username"), default: [:]]["profile"] = profile
        case "InboxListResult": conversations = m.objects("conversations")
        case "InboxResult":
            let user = m.string("targetUsername"), batch = m.objects("messages")
            let existing = (p?.fields.int("beforeId") ?? 0) > 0 ? messages[user, default: []] : []
            messages[user] = merge(existing, batch); hasOlder[user] = batch.count >= 100; request("InboxListRequest")
        case "DirectMessageResult", "DirectMessageReceived":
            let message = m.object("message")
            let user = message.string("senderUsername") == client?.username ? message.string("recipientUsername") : message.string("senderUsername")
            messages[user] = merge(messages[user, default: []], [message])
            if foreground && openConversation == user { action("InboxMarkRead", user: user) }
            request("InboxListRequest"); if type == "DirectMessageResult" { status = "Message sent" }
        case "InboxDeleteResult":
            let user = m.string("targetUsername")
            if m.string("action") == "InboxConversationDelete" { messages.removeValue(forKey: user); if openConversation == user { openConversation = "" } }
            else { for key in Array(messages.keys) { messages[key] = messages[key]?.filter { $0.int("id") != m.int("messageId") } } }
            request("InboxListRequest"); status = "Deleted from your inbox"
        case "UnreadCountChanged":
            if let i = conversations.firstIndex(where: { $0.string("username") == m.string("username") }) { conversations[i]["unreadCount"] = m.int("unreadCount") }
        case "SocialChanged", "SocialActionResult":
            refresh(); if !query.isEmpty { search(query) }
            if type == "SocialActionResult" { for user in Array(profiles.keys) { profile(user) }; status = m.string("action") == "FriendRequestSend" ? "Friend request sent" : "Saved" }
        case "PresenceChanged":
            let user = m.string("username")
            func updated(_ entries: [JSON]) -> [JSON] { entries.map { item in
                guard item.object("profile").string("username") == user else { return item }
                var item = item; item["presenceStatus"] = m.string("status"); item["hasRoom"] = m.bool("hasRoom"); item["room"] = m.object("room"); return item
            } }
            friends = updated(friends); users = updated(users)
            if let entry = profiles[user] { profiles[user] = updated([entry]).first }
            if let i = conversations.firstIndex(where: { $0.string("username") == user }) { conversations[i]["presence"] = m.string("status") }
        case "RoomInviteReceived": invitations.append(m); if invitations.count > 50 { invitations.removeFirst() }
        case "RoomInviteResult": status = "Room invitation sent"
        case "PlayerMuteList": mutedText = []; mutedVoice = []; for item in m.objects("preferences") { updateMute(item) }
        case "PlayerMuteResult": updateMute(m); status = "Mute preference saved"
        case "ServerAdminData": if m.string("serverId") == client?.room.string("serverId") { admin = m }
        case "ModerationResult", "StaffRoleResult": refreshAdmin(); status = "Server settings updated"
        case "PlayerReportResult": status = m.bool("automaticBanApplied") ? "Report received; enforcement applied" : "Report received for moderator review"
        case "Error": guard p != nil else { return false }; status = m.string("message", "Request failed")
        default: return false
        }
        update(); return true
    }
    private func merge(_ old: [JSON], _ new: [JSON]) -> [JSON] {
        var byID: [Int: JSON] = [:]; for m in old + new { byID[m.int("id")] = m }; return byID.values.sorted { $0.int("id") < $1.int("id") }
    }
    private func updateMute(_ m: JSON) {
        let user = m.string("targetUsername")
        if m.bool("textChat") { mutedText.insert(user) } else { mutedText.remove(user) }
        if m.bool("voiceChat") { mutedVoice.insert(user) } else { mutedVoice.remove(user) }
    }
    func clear() {
        pending = [:]; latest = [:]; friends = []; requests = []; notifications = []; blocked = []; conversations = []; users = []
        profiles = [:]; messages = [:]; hasOlder = [:]; invitations = []; mutedText = []; mutedVoice = []; admin = [:]; openConversation = ""; status = ""; query = ""
        update()
    }
}
