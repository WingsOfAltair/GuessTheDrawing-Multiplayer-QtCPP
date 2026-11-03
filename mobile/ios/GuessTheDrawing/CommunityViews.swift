import SwiftUI

private func socialDate(_ seconds: Double) -> String {
    seconds <= 0 ? "Permanent" : Date(timeIntervalSince1970: seconds).formatted(date: .abbreviated, time: .shortened)
}
struct PlayerDestination: Identifiable {
    let user: String
    var action = "profile"
    var id: String { user + ":" + action }
}
@MainActor
struct CommunityStatus: View {
    @ObservedObject var social: CommunityClient
    var body: some View {
        HStack { if social.busy { ProgressView() }; if !social.status.isEmpty { Text(social.status).font(.footnote) }; Spacer() }
            .padding(.horizontal).padding(.vertical, social.busy || !social.status.isEmpty ? 6 : 0)
    }
}
@MainActor
struct PlayerIdentity: View {
    var profile: JSON
    var detail = ""
    @EnvironmentObject private var theme: AppTheme
    var body: some View {
        HStack(spacing: 12) {
            ZStack {
                Circle().fill(theme.accent.opacity(0.15))
                Text(String(profile.string("displayName", profile.string("username")).prefix(1)).uppercased()).font(.title2.bold()).foregroundStyle(theme.accent)
                if let url = URL(string: profile.string("avatarUrl")), ["https", "http"].contains(url.scheme ?? "") {
                    AsyncImage(url: url) { image in image.resizable().scaledToFill() } placeholder: { Color.clear }
                }
            }.frame(width: 48, height: 48).clipShape(Circle())
            VStack(alignment: .leading, spacing: 4) {
                Text(profile.string("displayName", profile.string("username"))).font(.headline)
                HStack(spacing: 5) {
                    Text("@" + profile.string("username"))
                    CountryFlag(code: profile.string("countryCode"))
                }.font(.caption).foregroundStyle(theme.secondary)
                if !detail.isEmpty { Text(detail).font(.subheadline).foregroundStyle(theme.secondary) }
            }
            Spacer(minLength: 0)
        }.padding(.vertical, 6)
    }
}
struct CountryFlag: View {
    let code: String
    var body: some View {
        if !code.isEmpty {
            if let path = Bundle.main.path(forResource: code.lowercased(), ofType: "png", inDirectory: "country-flags"), let image = UIImage(contentsOfFile: path) {
                Image(uiImage: image).resizable().scaledToFit().frame(width: 24, height: 18).accessibilityHidden(true)
            }
            Text(code)
        }
    }
}
@MainActor
struct InteractivePlayerRow: View {
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let profile: JSON
    var detail = ""
    @State private var destination: PlayerDestination?
    var body: some View {
        let user = profile.string("username")
        Button { destination = PlayerDestination(user: user) } label: { PlayerIdentity(profile: profile, detail: detail) }
            .buttonStyle(.plain).gtdRow().contentShape(Rectangle())
            .simultaneousGesture(LongPressGesture(minimumDuration: 0.2).onEnded { _ in social.profile(user) })
            .contextMenu {
                Button("View profile") { destination = PlayerDestination(user: user) }
                PlayerActions(client: client, social: social, user: user) { destination = PlayerDestination(user: user, action: $0) }
            }
            .sheet(item: $destination) { route in PlayerDestinationView(client: client, social: social, destination: route) }
    }
}
@MainActor
struct CommunityHub: View {
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    @Environment(\.dismiss) private var dismiss
    @State private var selection: String
    @State private var query = ""
    @State private var destination: PlayerDestination?
    @State private var deleteUser = ""
    @State private var confirmDelete = false
    private let embedded: Bool
    init(client: GameClient, initial: String = "Friends", embedded: Bool = false) {
        self.client = client; self.social = client.community; self.embedded = embedded; _selection = State(initialValue: initial)
    }
    private var sections: [String] { ["Friends", "Requests", "Inbox", "Notifications", "Find players", "Blocked"] + (social.canModerate ? ["Moderation"] : []) }
    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                CommunityStatus(social: social)
                Picker("Section", selection: $selection) { ForEach(sections, id: \.self) { Text($0).tag($0) } }.pickerStyle(.menu).padding(.horizontal)
                if selection == "Find players" {
                    HStack { TextField("Username or display name", text: $query).textFieldStyle(.roundedBorder).submitLabel(.search).onSubmit { social.search(query) }
                        Button("Search") { social.search(query) } }.padding()
                }
                List { contents }.listStyle(.insetGrouped).gtdSurface().refreshable { refresh() }
            }.gtdSurface().navigationTitle("People & messages").navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .navigationBarLeading) { if !embedded { Button("Close") { dismiss() } } }; ToolbarItem(placement: .navigationBarTrailing) { Button { refresh() } label: { Image(systemName: "arrow.clockwise") }.accessibilityLabel("Refresh") } }
                .onAppear { query = social.query; refresh() }
                .onChange(of: selection) { _ in if selection == "Moderation" { social.refreshAdmin() } }
                .onChange(of: client.room.string("roomId")) { _ in dismiss() }
                .sheet(item: $destination) { PlayerDestinationView(client: client, social: social, destination: $0) }
                .confirmationDialog("Delete your copy of this conversation?", isPresented: $confirmDelete, titleVisibility: .visible) {
                    Button("Delete", role: .destructive) { social.action("InboxConversationDelete", user: deleteUser) }
                }
        }
    }
    private func refresh() { social.refresh(); if selection == "Moderation" { social.refreshAdmin() }; if selection == "Find players" { social.search(query) } }
    @ViewBuilder private var contents: some View {
        switch selection {
        case "Friends", "Find players":
            let entries = selection == "Friends" ? social.friends : social.users
            if entries.isEmpty { empty(selection == "Friends" ? "Make your first connection" : "Find your people", detail: selection == "Friends" ? "Search for a player to send a friend request." : "Search by username or display name.", icon: "person.2") }
            ForEach(Array(entries.enumerated()), id: \.offset) { _, entry in
                InteractivePlayerRow(client: client, social: social, profile: entry.object("profile"), detail: entry.string("presenceStatus") + " · " + entry.string("relationship"))
            }
        case "Requests": requestRows
        case "Inbox":
            if social.conversations.isEmpty { empty("Start a conversation", detail: "Open a friend's profile and choose Message.", icon: "bubble.left.and.bubble.right") }
            ForEach(Array(social.conversations.enumerated()), id: \.offset) { _, conversation in
                let user = conversation.string("username")
                Button { destination = PlayerDestination(user: user, action: "message") } label: {
                    HStack { PlayerIdentity(profile: conversation, detail: conversation.string("presence")); if conversation.int("unreadCount") > 0 { Text("\(conversation.int("unreadCount"))").font(.caption.bold()).padding(8).background(Color.accentColor.opacity(0.15), in: Capsule()) } }
                }.buttonStyle(.plain).gtdRow().contextMenu {
                    Button("View profile") { destination = PlayerDestination(user: user) }
                    Button("Delete conversation", role: .destructive) { deleteUser = user; confirmDelete = true }
                }
            }
        case "Notifications":
            if social.notifications.isEmpty && social.invitations.isEmpty { empty("You're all caught up", detail: "Friend updates and room invitations appear here.", icon: "bell.badge") }
            invitationRows
            ForEach(Array(social.notifications.enumerated()), id: \.offset) { _, item in notificationRow(item) }
        case "Blocked":
            if social.blocked.isEmpty { empty("No blocked players", detail: "You can manage blocked accounts here.", icon: "person.crop.circle.badge.checkmark") }
            ForEach(social.blocked, id: \.self) { user in HStack {
                Button("@"+user) { destination = PlayerDestination(user: user) }; Spacer()
                Button("Unblock") { social.action("BlockSet", user: user, fields: ["blocked": false]) }
            }.gtdRow() }
        case "Moderation": ModerationRows(client: client, social: social) { destination = $0 }
        default: EmptyView()
        }
    }
    private func empty(_ title: String, detail: String, icon: String) -> some View {
        VStack(spacing: 12) { Image(systemName: icon).font(.largeTitle).foregroundStyle(.tint); Text(title).font(.headline); Text(detail).font(.subheadline).foregroundStyle(.secondary).multilineTextAlignment(.center) }.frame(maxWidth: .infinity).padding(.vertical, 30).gtdRow()
    }
    @ViewBuilder private var requestRows: some View {
        if social.requests.isEmpty && !social.notifications.contains(where: { $0.string("kind") == "friend_request_accepted" }) { empty("No pending requests", detail: "New requests will appear here.", icon: "person.badge.plus") }
        ForEach(Array(social.requests.enumerated()), id: \.offset) { _, request in
            let outgoing = request.string("senderUsername") == client.username
            let user = request.string(outgoing ? "recipientUsername" : "senderUsername")
            VStack(alignment: .leading, spacing: 10) {
                Button((outgoing ? "To @" : "From @") + user) { destination = PlayerDestination(user: user) }.font(.headline)
                Text(request.string("status")).font(.caption).foregroundStyle(.secondary)
                if !outgoing && request.string("status") == "pending" {
                    HStack {
                        Button("Accept") { social.action("FriendRequestAccept", fields: ["friendRequestId": request.int("id")]) }.buttonStyle(.borderedProminent)
                        Button("Decline") { social.action("FriendRequestDeny", fields: ["friendRequestId": request.int("id")]) }.buttonStyle(.bordered)
                    }
                } else if request.string("status") == "denied" {
                    Button("Clear declined request") { social.action("FriendRequestClear", fields: ["friendRequestId": request.int("id")]); social.action("FriendRequestNotificationClear", user: user) }
                }
            }.gtdRow()
        }
        ForEach(Array(social.notifications.filter { $0.string("kind") == "friend_request_accepted" }.enumerated()), id: \.offset) { _, item in notificationRow(item) }
    }
    private func notificationRow(_ item: JSON) -> some View {
        let user = "@" + item.string("relatedUsername")
        let kind = item.string("kind")
        let message = kind == "friends_now" ? "You and \(user) are now friends" : user + (kind == "friend_request_accepted" ? " accepted your friend request" : " declined your friend request")
        return Button { social.action("NotificationClear", fields: ["notificationId": item.int("id")]) } label: {
            VStack(alignment: .leading, spacing: 6) { Text(message); Text(socialDate(item.number("createdAt")) + " · Tap to clear").font(.caption).foregroundStyle(.secondary) }
        }.buttonStyle(.plain).gtdRow()
    }
    @ViewBuilder private var invitationRows: some View {
        ForEach(Array(social.invitations.enumerated()), id: \.offset) { index, invite in
            VStack(alignment: .leading, spacing: 10) {
                Text("@\(invite.string("fromUsername")) invited you to \(invite.object("room").string("roomName"))").font(.headline)
                HStack {
                    Button("View invitation") { destination = PlayerDestination(user: invite.string("fromUsername"), action: "invite") }
                    Button("Dismiss") { social.dismissInvitation(index) }
                }.buttonStyle(.bordered)
            }.gtdRow()
        }
    }
}
@MainActor
struct PlayerActions: View {
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let user: String
    let open: (String) -> Void
    private var entry: JSON { social.profiles[user] ?? social.friends.first { $0.object("profile").string("username") == user } ?? [:] }
    private var inRoom: Bool { client.players.contains { $0.object("profile").string("username") == user } }
    var body: some View {
        if user != client.username {
            switch entry.string("relationship", "none") {
            case "none": Button("Send friend request") { social.action("FriendRequestSend", user: user) }
            case "incoming":
                Button("Accept friend request") { social.action("FriendRequestAccept", fields: ["friendRequestId": entry.int("relationshipId")]) }
                Button("Decline friend request") { social.action("FriendRequestDeny", fields: ["friendRequestId": entry.int("relationshipId")]) }
            case "denied": Button("Clear declined request") { social.action("FriendRequestClear", fields: ["friendRequestId": entry.int("relationshipId")]); social.action("FriendRequestNotificationClear", user: user) }
            case "friend":
                Button("Message") { open("message") }
                if client.connected { Button("Invite to my room") { social.action("RoomInviteSend", user: user) } }
                Button("Remove friend", role: .destructive) { open("remove") }
            default: EmptyView()
            }
            Button(entry.string("relationship") == "blocked" ? "Unblock user" : "Block user") { open(entry.string("relationship") == "blocked" ? "unblock" : "block") }
            if entry.bool("hasRoom") { Button("Join player's room") { open("join") }; Button("Watch player's room") { open("watch") } }
            let target = social.targetProfile(user), targetLevel = CommunityPolicy.level(target)
            if client.connected && targetLevel == 0 && (inRoom || target.bool("roomRoleLoaded")) {
                Button(social.mutedText.contains(user) ? "Unmute text" : "Mute text") { social.mute(user, text: !social.mutedText.contains(user), voice: social.mutedVoice.contains(user)) }
                Button(social.mutedVoice.contains(user) ? "Unmute voice" : "Mute voice") { social.mute(user, text: social.mutedText.contains(user), voice: !social.mutedVoice.contains(user)) }
                if inRoom { Button("Report player") { open("report") } }
            }
            if social.canModerate && social.level > targetLevel && (inRoom || target.bool("roomRoleLoaded")) {
                Button("Kick player…", role: .destructive) { open("kick") }
                Button("Ban player…", role: .destructive) { open("ban") }
                Button("Server mute…") { open("mute") }
                if social.level >= 2 { Button("Set server role…") { open("role") } }
            }
        }
    }
}
@MainActor
struct PlayerDestinationView: View {
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let destination: PlayerDestination
    var body: some View {
        switch destination.action {
        case "profile": PlayerProfileSheet(client: client, social: social, user: destination.user)
        case "message": InboxView(client: client, social: social, user: destination.user)
        default: PlayerActionForm(client: client, social: social, destination: destination)
        }
    }
}
@MainActor
struct PlayerProfileSheet: View {
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let user: String
    @Environment(\.dismiss) private var dismiss
    @State private var destination: PlayerDestination?
    var body: some View {
        let entry = social.profiles[user] ?? [:], p = social.targetProfile(user)
        NavigationStack {
            VStack(spacing: 0) {
                CommunityStatus(social: social)
                List {
                    Section { PlayerIdentity(profile: p, detail: entry.string("presenceStatus", "Loading…")) }.gtdRow()
                    Section("Game stats") {
                        stat("Total score", "\(p.int("totalScore"))")
                        stat("Guessing", "\(p.int("guessingScore")) · \(p.string("guessingRank"))")
                        stat("Drawing", "\(p.int("drawingScore")) · \(p.string("drawingRank"))")
                        stat("Wins / losses", "\(p.int("roundsWon")) / \(p.int("roundsLost"))")
                        stat("Abandoned rounds", "\(p.int("roundsAbandoned"))")
                        stat("Leaderboard", "#\(p.int("leaderboardRank"))")
                        stat("Guessing leaderboard", "#\(p.int("guessingLeaderboardRank"))")
                        stat("Drawing leaderboard", "#\(p.int("drawingLeaderboardRank"))")
                    }.gtdRow()
                    Section("Account") {
                        stat("Relationship", entry.string("relationship", "Loading…"))
                        HStack { Text("Country"); Spacer(); if p.string("countryCode").isEmpty { Text("Not set") } else { CountryFlag(code: p.string("countryCode")) } }
                        stat("Role", p.bool("masterSuperAdmin") ? "Master super-admin" : p.string("serverRole", "player"))
                        if !p.string("avatarUrl").isEmpty { stat("Avatar", p.string("avatarUrl")) }
                        if entry.bool("hasRoom") { stat("Current room", entry.object("room").string("roomName")) }
                    }.gtdRow()
                    if !entry.objects("activeBans").isEmpty { Section("Active bans") {
                        ForEach(Array(entry.objects("activeBans").enumerated()), id: \.offset) { _, ban in
                            VStack(alignment: .leading) { Text(ban.string("serverId")).font(.headline); Text(ban.string("reason")); Text(socialDate(ban.number("expiresAt"))).font(.caption) }
                        }
                    }.gtdRow() }
                    if entry["relationship"] != nil { Section("Player actions") { PlayerActions(client: client, social: social, user: user) { destination = PlayerDestination(user: user, action: $0) } }.gtdRow() }
                }.gtdSurface()
            }.gtdSurface().navigationTitle("Player profile").navigationBarTitleDisplayMode(.inline)
                .toolbar { Button("Done") { dismiss() } }.onAppear { social.profile(user) }
                .sheet(item: $destination) { PlayerDestinationView(client: client, social: social, destination: $0) }
                .onChange(of: client.room.string("roomId")) { _ in dismiss() }
        }
    }
    private func stat(_ name: String, _ value: String) -> some View { HStack(alignment: .top) { Text(name).foregroundStyle(.secondary); Spacer(); Text(value).multilineTextAlignment(.trailing) } }
}
@MainActor
struct InboxView: View {
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let user: String
    @Environment(\.dismiss) private var dismiss
    @State private var text = ""
    @State private var atBottom = true
    @State private var deleteID = 0
    @State private var confirmDelete = false
    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                CommunityStatus(social: social)
                ScrollViewReader { reader in
                    List {
                        if social.hasOlder[user] == true { Button("Load older messages") { atBottom = false; social.inbox(user, older: true) }.gtdRow() }
                        ForEach(Array(social.messages[user, default: []].enumerated()), id: \.element.intID) { _, message in
                            VStack(alignment: .leading, spacing: 8) {
                                Text(message.string("body"))
                                Text("@\(message.string("senderUsername")) · \(socialDate(message.number("createdAt")))").font(.caption).foregroundStyle(.secondary)
                            }.frame(maxWidth: .infinity, alignment: message.string("senderUsername") == client.username ? .trailing : .leading).gtdRow()
                                .contextMenu { Button("Delete message", role: .destructive) { deleteID = message.int("id"); confirmDelete = true } }
                        }
                        Color.clear.frame(height: 1).id("bottom").listRowBackground(Color.clear)
                            .onAppear { atBottom = true; social.openConversation = user; social.action("InboxMarkRead", user: user) }
                            .onDisappear { atBottom = false; if social.openConversation == user { social.openConversation = "" } }
                    }.gtdSurface().onChange(of: social.messages[user, default: []].count) { _ in if atBottom { reader.scrollTo("bottom", anchor: .bottom) } }
                }
                HStack {
                    TextField("Private message", text: $text).textFieldStyle(.roundedBorder).submitLabel(.send).onSubmit(send)
                    Button(action: send) { Image(systemName: "arrow.up.circle.fill").font(.title) }.disabled(text.trimmingCharacters(in: .whitespaces).isEmpty).accessibilityLabel("Send private message")
                }.padding()
            }.gtdSurface().navigationTitle("Inbox · @"+user).navigationBarTitleDisplayMode(.inline)
                .toolbar { Button("Done") { dismiss() } }
                .onAppear { social.inbox(user) }.onDisappear { if social.openConversation == user { social.openConversation = "" } }
                .confirmationDialog("Delete this message from your view?", isPresented: $confirmDelete, titleVisibility: .visible) { Button("Delete", role: .destructive) { social.action("InboxMessageDelete", fields: ["messageId": deleteID]) } }
        }
    }
    private func send() { let body = text.trimmingCharacters(in: .whitespacesAndNewlines); if !body.isEmpty && social.action("DirectMessageSend", user: user, fields: ["body": String(body.prefix(2000))]) { text = "" } }
}
extension Dictionary where Key == String, Value == Any { var intID: Int { int("id") } }
@MainActor
struct ModerationRows: View {
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let open: (PlayerDestination) -> Void
    @State private var revoke: JSON = [:]
    @State private var confirm = false
    var body: some View {
        if social.canModerate {
            ForEach(["bans", "mutes"], id: \.self) { kind in Section(kind.capitalized) {
                ForEach(Array(social.admin.objects(kind).enumerated()), id: \.offset) { _, item in
                    VStack(alignment: .leading, spacing: 8) {
                        Button("@"+item.string("targetUsername")) { open(PlayerDestination(user: item.string("targetUsername"))) }.font(.headline)
                        Text(item.string("reason"))
                        if kind == "mutes" { Text((item.bool("textChat") ? "Text " : "") + (item.bool("voiceChat") ? "Voice" : "")).font(.caption) }
                        Text("By @\(item.string("issuedBy")) · \(socialDate(item.number("expiresAt")))").font(.caption).foregroundStyle(.secondary)
                        Button(kind == "bans" ? "Unban" : "Unmute") { revoke = item; revoke["action"] = kind == "bans" ? "unban" : "unmute"; confirm = true }
                    }.gtdRow()
                }
            } }
            Section("Staff") {
                ForEach(Array(social.admin.objects("staff").enumerated()), id: \.offset) { _, item in Button {
                    open(PlayerDestination(user: item.string("username")))
                } label: { VStack(alignment: .leading) { Text("@"+item.string("username")); Text(item.string("role") + " · assigned by @"+item.string("assignedBy")).font(.caption) } }.gtdRow() }
                if social.level >= 2 { Button("Assign staff role") { open(PlayerDestination(user: "", action: "role")) }.gtdRow() }
            }.confirmationDialog("Revoke this sanction?", isPresented: $confirm, titleVisibility: .visible) {
                Button("Revoke") { social.roomRequest("ModerationRequest", user: revoke.string("targetUsername"), fields: ["action": revoke.string("action"), "sanctionId": revoke.int("id")]) }
            }
        } else { Text("Your current role cannot manage this server.").gtdRow() }
    }
}
@MainActor
struct PlayerActionForm: View {
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let destination: PlayerDestination
    @Environment(\.dismiss) private var dismiss
    @State private var username = "", reason = "", passkey = "", error = "", role = "player"
    @State private var textChat = true, voiceChat = true, watch = false
    @State private var mode = "duration", amount = "1", unit = 3600
    @State private var until = Date().addingTimeInterval(86400)
    private var action: String { destination.action }
    private var room: JSON {
        if action == "invite" { return social.invitations.first { $0.string("fromUsername") == destination.user }?.object("room") ?? [:] }
        return social.profiles[destination.user]?.object("room") ?? [:]
    }
    var body: some View {
        NavigationStack {
            Form {
                Section { Text("@" + destination.user).font(.title2.bold()); CommunityStatus(social: social) }.gtdRow()
                if ["join", "watch", "invite"].contains(action) {
                    Section("Join room") { Text(room.string("roomName")); if client.connected { Text("This will leave your current room.").foregroundStyle(.secondary) }; Toggle("Watch as spectator", isOn: $watch); if room.bool("privateRoom") { SecureField("Room passkey", text: $passkey) } }.gtdRow()
                } else if action == "role" {
                    Section("Server role") { TextField("Account username", text: $username).textInputAutocapitalization(.never); Picker("Role", selection: $role) { Text("Player").tag("player"); Text("Moderator").tag("moderator"); if social.level >= 3 { Text("Admin").tag("admin") } } }.gtdRow()
                } else if ["kick", "ban", "mute", "report"].contains(action) {
                    Section("Reason") { TextField("Explain the reason", text: $reason) }.gtdRow()
                    if action == "mute" { Section("Mute channels") { Toggle("Text chat", isOn: $textChat); Toggle("Voice chat", isOn: $voiceChat) }.gtdRow() }
                    if ["ban", "mute"].contains(action) { Section("Expires") {
                        Picker("Duration", selection: $mode) { Text("For a duration").tag("duration"); Text("Until date and time").tag("date"); Text("Permanent").tag("permanent") }
                        if mode == "duration" { TextField("Amount", text: $amount).keyboardType(.numberPad); Picker("Unit", selection: $unit) { ForEach(CommunityPolicy.durationUnits, id: \.1) { Text($0.0).tag($0.1) } } }
                        if mode == "date" { DatePicker("Until", selection: $until, in: Date()..., displayedComponents: [.date, .hourAndMinute]) }
                    }.gtdRow() }
                } else { Text(action == "remove" ? "Remove this player from your friends?" : action == "block" ? "Block this player?" : "Unblock this player?").gtdRow() }
                if !error.isEmpty { Text(error).foregroundStyle(.red).gtdRow() }
                Button(["join", "watch", "invite"].contains(action) ? "Join room" : "Confirm " + action, action: submit).gtdRow()
            }.gtdSurface().navigationTitle(action.capitalized).navigationBarTitleDisplayMode(.inline)
                .toolbar { Button("Cancel") { dismiss() } }.onAppear { username = destination.user; watch = action == "watch" }
        }
    }
    private func submit() {
        error = ""
        if ["join", "watch", "invite"].contains(action) {
            guard !room.isEmpty else { error = "Room is no longer available."; return }
            client.join(room, spectator: watch, passkey: passkey); dismiss(); return
        }
        if action == "role" {
            guard social.canModerate, social.level >= 2, !username.trimmingCharacters(in: .whitespaces).isEmpty else { error = "Enter a username and check your permissions."; return }
            if social.roomRequest("StaffRoleRequest", user: username.trimmingCharacters(in: .whitespaces), fields: ["role": role]) { dismiss() }; return
        }
        if ["remove", "block", "unblock"].contains(action) {
            if social.action(action == "remove" ? "FriendRemove" : "BlockSet", user: destination.user, fields: action == "remove" ? [:] : ["blocked": action == "block"]) { dismiss() }; return
        }
        let reason = reason.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !reason.isEmpty, reason.count <= 500, !reason.contains("\n") else { error = "Enter a single-line reason, up to 500 characters."; return }
        if action == "report" { if social.roomRequest("PlayerReport", user: destination.user, fields: ["reason": reason]) { dismiss() }; return }
        guard social.canModerate else { error = "You no longer have moderation permissions."; return }
        var fields: JSON = ["action": action, "reason": reason, "textChat": textChat, "voiceChat": voiceChat]
        if action == "mute" && !textChat && !voiceChat { error = "Select at least one chat channel."; return }
        if action != "kick" && mode != "permanent" {
            let expiry: Double
            if mode == "duration" { guard let n = Int(amount), (1...100000).contains(n) else { error = "Choose an amount between 1 and 100000."; return }; expiry = Date().timeIntervalSince1970 + Double(n) * Double(unit) }
            else { expiry = until.timeIntervalSince1970 }
            guard expiry > Date().timeIntervalSince1970 else { error = "Choose a future expiry."; return }; fields["expiresAt"] = Int(expiry)
        }
        if social.roomRequest("ModerationRequest", user: destination.user, fields: fields) { dismiss() }
    }
}
