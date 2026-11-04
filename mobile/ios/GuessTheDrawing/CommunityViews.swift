import SwiftUI

private func socialDate(_ seconds: Double) -> String {
    if seconds <= 0 { return L("Permanent") }
    let formatter = DateFormatter(); formatter.locale = L10n.locale; formatter.dateStyle = .medium; formatter.timeStyle = .short
    return formatter.string(from: Date(timeIntervalSince1970: seconds))
}
struct PlayerDestination: Identifiable {
    let user: String
    var action = "profile"
    var id: String { user + ":" + action }
}
@MainActor
struct CommunityStatus: View {
    @EnvironmentObject private var language: AppLanguage
    @ObservedObject var social: CommunityClient
    var body: some View {
        let _ = language.selection
        HStack { if social.busy { ProgressView() }; if !social.status.isEmpty { Text(L(social.status)).font(.footnote) }; Spacer() }
            .padding(.horizontal).padding(.vertical, social.busy || !social.status.isEmpty ? 6 : 0)
    }
}
@MainActor
struct PlayerIdentity: View {
    @EnvironmentObject private var language: AppLanguage
    var profile: JSON
    var detail = ""
    @EnvironmentObject private var theme: AppTheme
    var body: some View {
        let _ = language.selection
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
                if !detail.isEmpty { Text(L10n.detail(detail)).font(.subheadline).foregroundStyle(theme.secondary) }
            }
            Spacer(minLength: 0)
        }.padding(.vertical, 6)
    }
}
struct CountryFlag: View {
    @EnvironmentObject private var language: AppLanguage
    let code: String
    var body: some View {
        let _ = language.selection
        if !code.isEmpty {
            if let path = Bundle.main.path(forResource: code.lowercased(), ofType: "png", inDirectory: "country-flags"), let image = UIImage(contentsOfFile: path) {
                Image(uiImage: image).resizable().scaledToFit().frame(width: 24, height: 18).accessibilityHidden(true)
            }
            Text(verbatim: code).environment(\.layoutDirection, .leftToRight)
        }
    }
}
@MainActor
struct InteractivePlayerRow: View {
    @EnvironmentObject private var language: AppLanguage
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let profile: JSON
    var detail = ""
    @State private var destination: PlayerDestination?
    var body: some View {
        let _ = language.selection
        let user = profile.string("username")
        Button { destination = PlayerDestination(user: user) } label: { PlayerIdentity(profile: profile, detail: detail) }
            .buttonStyle(.plain).gtdRow().contentShape(Rectangle())
            .simultaneousGesture(LongPressGesture(minimumDuration: 0.2).onEnded { _ in social.profile(user) })
            .contextMenu {
                Button(L("View profile")) { destination = PlayerDestination(user: user) }
                PlayerActions(client: client, social: social, user: user) { destination = PlayerDestination(user: user, action: $0) }
            }
            .sheet(item: $destination) { route in PlayerDestinationView(client: client, social: social, destination: route) }
    }
}
@MainActor
struct CommunityHub: View {
    @EnvironmentObject private var language: AppLanguage
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
        let _ = language.selection
        NavigationStack {
            VStack(spacing: 0) {
                CommunityStatus(social: social)
                Picker(L("Section"), selection: $selection) { ForEach(sections, id: \.self) { Text(L($0)).tag($0) } }.pickerStyle(.menu).padding(.horizontal)
                if selection == "Find players" {
                    HStack { TextField(L("Username or display name"), text: $query).textFieldStyle(.roundedBorder).submitLabel(.search).onSubmit { social.search(query) }
                        Button(L("Search")) { social.search(query) } }.padding()
                }
                List { contents }.listStyle(.insetGrouped).gtdSurface().refreshable { refresh() }
            }.gtdSurface().navigationTitle(L("People & messages")).navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .navigationBarLeading) { if !embedded { Button(L("Close")) { dismiss() } } }; ToolbarItem(placement: .navigationBarTrailing) { Button { refresh() } label: { Image(systemName: "arrow.clockwise") }.accessibilityLabel(L("Refresh")) } }
                .onAppear { query = social.query; refresh() }
                .onChange(of: selection) { _ in if selection == "Moderation" { social.refreshAdmin() } }
                .onChange(of: client.room.string("roomId")) { _ in dismiss() }
                .sheet(item: $destination) { PlayerDestinationView(client: client, social: social, destination: $0) }
                .confirmationDialog(L("Delete your copy of this conversation?"), isPresented: $confirmDelete, titleVisibility: .visible) {
                    Button(L("Delete"), role: .destructive) { social.action("InboxConversationDelete", user: deleteUser) }
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
                InteractivePlayerRow(client: client, social: social, profile: entry.object("profile"), detail: L(entry.string("presenceStatus")) + " · " + L(entry.string("relationship")))
            }
        case "Requests": requestRows
        case "Inbox":
            if social.conversations.isEmpty { empty("Start a conversation", detail: "Open a friend's profile and choose Message.", icon: "bubble.left.and.bubble.right") }
            ForEach(Array(social.conversations.enumerated()), id: \.offset) { _, conversation in
                let user = conversation.string("username")
                Button { destination = PlayerDestination(user: user, action: "message") } label: {
                    HStack { PlayerIdentity(profile: conversation, detail: conversation.string("presence")); if conversation.int("unreadCount") > 0 { Text(L("\(conversation.int("unreadCount"))")).font(.caption.bold()).padding(8).background(Color.accentColor.opacity(0.15), in: Capsule()) } }
                }.buttonStyle(.plain).gtdRow().contextMenu {
                    Button(L("View profile")) { destination = PlayerDestination(user: user) }
                    Button(L("Delete conversation"), role: .destructive) { deleteUser = user; confirmDelete = true }
                }
            }
        case "Notifications":
            if social.notifications.isEmpty && social.invitations.isEmpty { empty("You're all caught up", detail: "Friend updates and room invitations appear here.", icon: "bell.badge") }
            invitationRows
            ForEach(Array(social.notifications.enumerated()), id: \.offset) { _, item in notificationRow(item) }
        case "Blocked":
            if social.blocked.isEmpty { empty("No blocked players", detail: "You can manage blocked accounts here.", icon: "person.crop.circle.badge.checkmark") }
            ForEach(social.blocked, id: \.self) { user in HStack {
                Button(L("@"+user)) { destination = PlayerDestination(user: user) }; Spacer()
                Button(L("Unblock")) { social.action("BlockSet", user: user, fields: ["blocked": false]) }
            }.gtdRow() }
        case "Moderation": ModerationRows(client: client, social: social) { destination = $0 }
        default: EmptyView()
        }
    }
    private func empty(_ title: String, detail: String, icon: String) -> some View {
        VStack(spacing: 12) { Image(systemName: icon).font(.largeTitle).foregroundStyle(.tint); Text(L(title)).font(.headline); Text(L10n.detail(detail)).font(.subheadline).foregroundStyle(.secondary).multilineTextAlignment(.center) }.frame(maxWidth: .infinity).padding(.vertical, 30).gtdRow()
    }
    @ViewBuilder private var requestRows: some View {
        if social.requests.isEmpty && !social.notifications.contains(where: { $0.string("kind") == "friend_request_accepted" }) { empty("No pending requests", detail: "New requests will appear here.", icon: "person.badge.plus") }
        ForEach(Array(social.requests.enumerated()), id: \.offset) { _, request in
            let outgoing = request.string("senderUsername") == client.username
            let user = request.string(outgoing ? "recipientUsername" : "senderUsername")
            VStack(alignment: .leading, spacing: 10) {
                Button(L10n.format(outgoing ? "To @%1" : "From @%1", [user])) { destination = PlayerDestination(user: user) }.font(.headline)
                Text(L(request.string("status"))).font(.caption).foregroundStyle(.secondary)
                if !outgoing && request.string("status") == "pending" {
                    HStack {
                        Button(L("Accept")) { social.action("FriendRequestAccept", fields: ["friendRequestId": request.int("id")]) }.buttonStyle(.borderedProminent)
                        Button(L("Decline")) { social.action("FriendRequestDeny", fields: ["friendRequestId": request.int("id")]) }.buttonStyle(.bordered)
                    }
                } else if request.string("status") == "denied" {
                    Button(L("Clear declined request")) { social.action("FriendRequestClear", fields: ["friendRequestId": request.int("id")]); social.action("FriendRequestNotificationClear", user: user) }
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
            VStack(alignment: .leading, spacing: 6) { Text(L(message)); Text(L(socialDate(item.number("createdAt")) + " · Tap to clear")).font(.caption).foregroundStyle(.secondary) }
        }.buttonStyle(.plain).gtdRow()
    }
    @ViewBuilder private var invitationRows: some View {
        ForEach(Array(social.invitations.enumerated()), id: \.offset) { index, invite in
            VStack(alignment: .leading, spacing: 10) {
                Text(L10n.format("@%1 invited you to %2", [invite.string("fromUsername"), invite.object("room").string("roomName")])).font(.headline)
                HStack {
                    Button(L("View invitation")) { destination = PlayerDestination(user: invite.string("fromUsername"), action: "invite") }
                    Button(L("Dismiss")) { social.dismissInvitation(index) }
                }.buttonStyle(.bordered)
            }.gtdRow()
        }
    }
}
@MainActor
struct PlayerActions: View {
    @EnvironmentObject private var language: AppLanguage
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let user: String
    let open: (String) -> Void
    private var entry: JSON { social.profiles[user] ?? social.friends.first { $0.object("profile").string("username") == user } ?? [:] }
    private var inRoom: Bool { client.players.contains { $0.object("profile").string("username") == user } }
    var body: some View {
        let _ = language.selection
        if user != client.username {
            switch entry.string("relationship", "none") {
            case "none": Button(L("Send friend request")) { social.action("FriendRequestSend", user: user) }
            case "incoming":
                Button(L("Accept friend request")) { social.action("FriendRequestAccept", fields: ["friendRequestId": entry.int("relationshipId")]) }
                Button(L("Decline friend request")) { social.action("FriendRequestDeny", fields: ["friendRequestId": entry.int("relationshipId")]) }
            case "denied": Button(L("Clear declined request")) { social.action("FriendRequestClear", fields: ["friendRequestId": entry.int("relationshipId")]); social.action("FriendRequestNotificationClear", user: user) }
            case "friend":
                Button(L("Message")) { open("message") }
                if client.connected { Button(L("Invite to my room")) { social.action("RoomInviteSend", user: user) } }
                Button(L("Remove friend"), role: .destructive) { open("remove") }
            default: EmptyView()
            }
            Button(L(entry.string("relationship") == "blocked" ? "Unblock user" : "Block user")) { open(entry.string("relationship") == "blocked" ? "unblock" : "block") }
            if entry.bool("hasRoom") { Button(L("Join player's room")) { open("join") }; Button(L("Watch player's room")) { open("watch") } }
            let target = social.targetProfile(user), targetLevel = CommunityPolicy.level(target)
            if client.connected && targetLevel == 0 && (inRoom || target.bool("roomRoleLoaded")) {
                Button(L(social.mutedText.contains(user) ? "Unmute text" : "Mute text")) { social.mute(user, text: !social.mutedText.contains(user), voice: social.mutedVoice.contains(user)) }
                Button(L(social.mutedVoice.contains(user) ? "Unmute voice" : "Mute voice")) { social.mute(user, text: social.mutedText.contains(user), voice: !social.mutedVoice.contains(user)) }
                if inRoom { Button(L("Report player")) { open("report") } }
            }
            if social.canModerate && social.level > targetLevel && (inRoom || target.bool("roomRoleLoaded")) {
                Button(L("Kick player…"), role: .destructive) { open("kick") }
                Button(L("Ban player…"), role: .destructive) { open("ban") }
                Button(L("Server mute…")) { open("mute") }
                if social.level >= 2 { Button(L("Set server role…")) { open("role") } }
            }
        }
    }
}
@MainActor
struct PlayerDestinationView: View {
    @EnvironmentObject private var language: AppLanguage
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let destination: PlayerDestination
    var body: some View {
        let _ = language.selection
        switch destination.action {
        case "profile": PlayerProfileSheet(client: client, social: social, user: destination.user)
        case "message": InboxView(client: client, social: social, user: destination.user)
        default: PlayerActionForm(client: client, social: social, destination: destination)
        }
    }
}
@MainActor
struct PlayerProfileSheet: View {
    @EnvironmentObject private var language: AppLanguage
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let user: String
    @Environment(\.dismiss) private var dismiss
    @State private var destination: PlayerDestination?
    var body: some View {
        let _ = language.selection
        let entry = social.profiles[user] ?? [:], p = social.targetProfile(user)
        NavigationStack {
            VStack(spacing: 0) {
                CommunityStatus(social: social)
                List {
                    Section { PlayerIdentity(profile: p, detail: entry.string("presenceStatus", "Loading…")) }.gtdRow()
                    Section(L("Game stats")) {
                        stat("Total score", "\(p.int("totalScore"))")
                        stat("Guessing", "\(p.int("guessingScore")) · \(L(p.string("guessingRank")))")
                        stat("Drawing", "\(p.int("drawingScore")) · \(L(p.string("drawingRank")))")
                        stat("Wins / losses", "\(p.int("roundsWon")) / \(p.int("roundsLost"))")
                        stat("Abandoned rounds", "\(p.int("roundsAbandoned"))")
                        stat("Leaderboard", "#\(p.int("leaderboardRank"))")
                        stat("Guessing leaderboard", "#\(p.int("guessingLeaderboardRank"))")
                        stat("Drawing leaderboard", "#\(p.int("drawingLeaderboardRank"))")
                    }.gtdRow()
                    Section(L("Account")) {
                        stat("Relationship", L(entry.string("relationship", "Loading…")))
                        HStack { Text(L("Country")); Spacer(); if p.string("countryCode").isEmpty { Text(L("Not set")) } else { CountryFlag(code: p.string("countryCode")) } }
                        stat("Role", L(p.bool("masterSuperAdmin") ? "Master super-admin" : p.string("serverRole", "player")))
                        if !p.string("avatarUrl").isEmpty { stat("Avatar", p.string("avatarUrl")) }
                        if entry.bool("hasRoom") { stat("Current room", entry.object("room").string("roomName")) }
                    }.gtdRow()
                    if !entry.objects("activeBans").isEmpty { Section(L("Active bans")) {
                        ForEach(Array(entry.objects("activeBans").enumerated()), id: \.offset) { _, ban in
                            VStack(alignment: .leading) { Text(ban.string("serverId")).font(.headline); Text(ban.string("reason")); Text(L(socialDate(ban.number("expiresAt")))).font(.caption) }
                        }
                    }.gtdRow() }
                    if entry["relationship"] != nil { Section(L("Player actions")) { PlayerActions(client: client, social: social, user: user) { destination = PlayerDestination(user: user, action: $0) } }.gtdRow() }
                }.gtdSurface()
            }.gtdSurface().navigationTitle(L("Player profile")).navigationBarTitleDisplayMode(.inline)
                .toolbar { Button(L("Done")) { dismiss() } }.onAppear { social.profile(user) }
                .sheet(item: $destination) { PlayerDestinationView(client: client, social: social, destination: $0) }
                .onChange(of: client.room.string("roomId")) { _ in dismiss() }
        }
    }
    private func stat(_ name: String, _ value: String) -> some View { HStack(alignment: .top) { Text(L(name)).foregroundStyle(.secondary); Spacer(); Text(L10n.isolate(value)).multilineTextAlignment(.trailing) } }
}
@MainActor
struct InboxView: View {
    @EnvironmentObject private var language: AppLanguage
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let user: String
    @Environment(\.dismiss) private var dismiss
    @State private var text = ""
    @State private var atBottom = true
    @State private var deleteID = 0
    @State private var confirmDelete = false
    var body: some View {
        let _ = language.selection
        NavigationStack {
            VStack(spacing: 0) {
                CommunityStatus(social: social)
                ScrollViewReader { reader in
                    List {
                        if social.hasOlder[user] == true { Button(L("Load older messages")) { atBottom = false; social.inbox(user, older: true) }.gtdRow() }
                        ForEach(Array(social.messages[user, default: []].enumerated()), id: \.element.intID) { _, message in
                            VStack(alignment: .leading, spacing: 8) {
                                Text(message.string("body"))
                                Text(L("@\(message.string("senderUsername")) · \(socialDate(message.number("createdAt")))")).font(.caption).foregroundStyle(.secondary)
                            }.frame(maxWidth: .infinity, alignment: message.string("senderUsername") == client.username ? .trailing : .leading).gtdRow()
                                .contextMenu { Button(L("Delete message"), role: .destructive) { deleteID = message.int("id"); confirmDelete = true } }
                        }
                        Color.clear.frame(height: 1).id("bottom").listRowBackground(Color.clear)
                            .onAppear { atBottom = true; social.openConversation = user; social.action("InboxMarkRead", user: user) }
                            .onDisappear { atBottom = false; if social.openConversation == user { social.openConversation = "" } }
                    }.gtdSurface().onChange(of: social.messages[user, default: []].count) { _ in if atBottom { reader.scrollTo("bottom", anchor: .bottom) } }
                }
                HStack {
                    TextField(L("Private message"), text: $text).textFieldStyle(.roundedBorder).submitLabel(.send).onSubmit(send)
                    Button(action: send) { Image(systemName: "arrow.up.circle.fill").font(.title) }.disabled(text.trimmingCharacters(in: .whitespaces).isEmpty).accessibilityLabel(L("Send private message"))
                }.padding()
            }.gtdSurface().navigationTitle(L("Inbox · @"+user)).navigationBarTitleDisplayMode(.inline)
                .toolbar { Button(L("Done")) { dismiss() } }
                .onAppear { social.inbox(user) }.onDisappear { if social.openConversation == user { social.openConversation = "" } }
                .confirmationDialog(L("Delete this message from your view?"), isPresented: $confirmDelete, titleVisibility: .visible) { Button(L("Delete"), role: .destructive) { social.action("InboxMessageDelete", fields: ["messageId": deleteID]) } }
        }
    }
    private func send() { let body = text.trimmingCharacters(in: .whitespacesAndNewlines); if !body.isEmpty && social.action("DirectMessageSend", user: user, fields: ["body": String(body.prefix(2000))]) { text = "" } }
}
extension Dictionary where Key == String, Value == Any { var intID: Int { int("id") } }
@MainActor
struct ModerationRows: View {
    @EnvironmentObject private var language: AppLanguage
    @ObservedObject var client: GameClient
    @ObservedObject var social: CommunityClient
    let open: (PlayerDestination) -> Void
    @State private var revoke: JSON = [:]
    @State private var confirm = false
    var body: some View {
        let _ = language.selection
        if social.canModerate {
            ForEach(["bans", "mutes"], id: \.self) { kind in Section(L(kind.capitalized)) {
                ForEach(Array(social.admin.objects(kind).enumerated()), id: \.offset) { _, item in
                    VStack(alignment: .leading, spacing: 8) {
                        Button(L("@"+item.string("targetUsername"))) { open(PlayerDestination(user: item.string("targetUsername"))) }.font(.headline)
                        Text(item.string("reason"))
                        if kind == "mutes" { Text(L(item.bool("textChat") && item.bool("voiceChat") ? "Text and voice" : item.bool("textChat") ? "Text chat" : "Voice chat")).font(.caption) }
                        Text(L("By @\(item.string("issuedBy")) · \(socialDate(item.number("expiresAt")))")).font(.caption).foregroundStyle(.secondary)
                        Button(L(kind == "bans" ? "Unban" : "Unmute")) { revoke = item; revoke["action"] = kind == "bans" ? "unban" : "unmute"; confirm = true }
                    }.gtdRow()
                }
            } }
            Section(L("Staff")) {
                ForEach(Array(social.admin.objects("staff").enumerated()), id: \.offset) { _, item in Button {
                    open(PlayerDestination(user: item.string("username")))
                } label: { VStack(alignment: .leading) { Text("@"+item.string("username")); Text(L10n.format("%1 · assigned by @%2", [L(item.string("role")), item.string("assignedBy")])).font(.caption) } }.gtdRow() }
                if social.level >= 2 { Button(L("Assign staff role")) { open(PlayerDestination(user: "", action: "role")) }.gtdRow() }
            }.confirmationDialog(L("Revoke this sanction?"), isPresented: $confirm, titleVisibility: .visible) {
                Button(L("Revoke")) { social.roomRequest("ModerationRequest", user: revoke.string("targetUsername"), fields: ["action": revoke.string("action"), "sanctionId": revoke.int("id")]) }
            }
        } else { Text(L("Your current role cannot manage this server.")).gtdRow() }
    }
}
@MainActor
struct PlayerActionForm: View {
    @EnvironmentObject private var language: AppLanguage
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
        let _ = language.selection
        NavigationStack {
            Form {
                Section { Text(L("@" + destination.user)).font(.title2.bold()); CommunityStatus(social: social) }.gtdRow()
                if ["join", "watch", "invite"].contains(action) {
                    Section(L("Join room")) { Text(room.string("roomName")); if client.connected { Text(L("This will leave your current room.")).foregroundStyle(.secondary) }; Toggle(L("Watch as spectator"), isOn: $watch); if room.bool("privateRoom") { SecureField(L("Room passkey"), text: $passkey) } }.gtdRow()
                } else if action == "role" {
                    Section(L("Server role")) { TextField(L("Account username"), text: $username).environment(\.layoutDirection, .leftToRight).textInputAutocapitalization(.never); Picker(L("Role"), selection: $role) { Text(L("Player")).tag("player"); Text(L("Moderator")).tag("moderator"); if social.level >= 3 { Text(L("Admin")).tag("admin") } } }.gtdRow()
                } else if ["kick", "ban", "mute", "report"].contains(action) {
                    Section(L("Reason")) { TextField(L("Explain the reason"), text: $reason) }.gtdRow()
                    if action == "mute" { Section(L("Mute channels")) { Toggle(L("Text chat"), isOn: $textChat); Toggle(L("Voice chat"), isOn: $voiceChat) }.gtdRow() }
                    if ["ban", "mute"].contains(action) { Section(L("Expires")) {
                        Picker(L("Duration"), selection: $mode) { Text(L("For a duration")).tag("duration"); Text(L("Until date and time")).tag("date"); Text(L("Permanent")).tag("permanent") }
                        if mode == "duration" { TextField(L("Amount"), text: $amount).keyboardType(.numberPad); Picker(L("Unit"), selection: $unit) { ForEach(CommunityPolicy.durationUnits, id: \.1) { Text(L($0.0)).tag($0.1) } } }
                        if mode == "date" { DatePicker(L("Until"), selection: $until, in: Date()..., displayedComponents: [.date, .hourAndMinute]) }
                    }.gtdRow() }
                } else { Text(L(action == "remove" ? "Remove this player from your friends?" : action == "block" ? "Block this player?" : "Unblock this player?")).gtdRow() }
                if !error.isEmpty { Text(L(error)).foregroundStyle(.red).gtdRow() }
                Button(L(["join", "watch", "invite"].contains(action) ? "Join room" : "Confirm"), action: submit).gtdRow()
            }.gtdSurface().navigationTitle(L(action.capitalized)).navigationBarTitleDisplayMode(.inline)
                .toolbar { Button(L("Cancel")) { dismiss() } }.onAppear { username = destination.user; watch = action == "watch" }
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
            if mode == "duration" { guard let n = L10n.integer(amount), (1...100000).contains(n) else { error = "Choose an amount between 1 and 100000."; return }; expiry = Date().timeIntervalSince1970 + Double(n) * Double(unit) }
            else { expiry = until.timeIntervalSince1970 }
            guard expiry > Date().timeIntervalSince1970 else { error = "Choose a future expiry."; return }; fields["expiresAt"] = Int(expiry)
        }
        if social.roomRequest("ModerationRequest", user: destination.user, fields: fields) { dismiss() }
    }
}
