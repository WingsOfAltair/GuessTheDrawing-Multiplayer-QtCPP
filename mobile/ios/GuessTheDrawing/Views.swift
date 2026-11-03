import SwiftUI

@MainActor
struct RootView: View {
    @ObservedObject var client: GameClient
    @StateObject private var theme = AppTheme()
    @Environment(\.scenePhase) private var scenePhase
    var body: some View {
        Group {
            if client.page == "login" { LoginView(client: client) }
            else if client.page == "game" { GameView(client: client) }
            else { LobbyView(client: client) }
        }.environmentObject(theme).preferredColorScheme(theme.scheme).tint(theme.accent)
            .onChange(of: scenePhase) { client.setForeground($0 == .active) }
    }
}
@MainActor
struct StatusView: View {
    @ObservedObject var client: GameClient
    var body: some View {
        HStack(spacing: 10) {
            if client.busy { ProgressView() }
            Text(client.status).font(.footnote).foregroundStyle(.secondary).frame(maxWidth: .infinity, alignment: .leading)
        }.padding(.horizontal).padding(.vertical, 6).accessibilityElement(children: .combine)
    }
}
@MainActor
struct LoginView: View {
    @ObservedObject var client: GameClient
    @AppStorage("masterHost") private var host = ""
    @AppStorage("masterPort") private var port = "45450"
    @AppStorage("username") private var username = ""
    @State private var password = ""
    @State private var displayName = ""
    @State private var create = false
    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Image(systemName: "pencil.and.outline").font(.system(size: 44)).foregroundStyle(.tint).accessibilityHidden(true)
                    Text("Draw together.\nGuess together.").font(.largeTitle.bold())
                    Text("Play with friends on desktop, iPhone, iPad and Android.").foregroundStyle(.secondary)
                }.listRowBackground(Color.clear)
                Section("Server") {
                    TextField("Master server address", text: $host).textContentType(.URL)
                        .keyboardType(.URL).textInputAutocapitalization(.never).autocorrectionDisabled()
                    TextField("Port", text: $port).keyboardType(.numberPad)
                }.gtdRow()
                Section("Account") {
                    TextField("Username", text: $username).textContentType(.username).textInputAutocapitalization(.never).autocorrectionDisabled()
                    SecureField("Password", text: $password).textContentType(create ? .newPassword : .password)
                    Toggle("Create an account", isOn: $create)
                    if create { TextField("Display name", text: $displayName) }
                    Button(create ? "Create account" : "Sign in") {
                        client.authenticate(host: host, port: Int(port) ?? 0, username: username,
                            password: password, name: displayName, create: create)
                        password = ""
                    }.disabled(client.busy).frame(maxWidth: .infinity)
                }.gtdRow()
                Section { StatusView(client: client) } footer: {
                    Text("Use your host computer's LAN address. Allow Local Network access when asked. Game version \(Wire.appVersion).")
                }
            }.gtdSurface().navigationTitle("Guess The Drawing").toolbar { ThemeMenu() }
        }
    }
}
@MainActor
struct LobbyView: View {
    @ObservedObject var client: GameClient
    var body: some View {
        TabView {
            RoomListView(client: client).tabItem { Label("Rooms", systemImage: "rectangle.3.group") }
            LeaderboardView(client: client).tabItem { Label("Ranks", systemImage: "trophy") }
            CommunityHub(client: client, embedded: true).tabItem { Label("People", systemImage: "person.2") }.badge(client.community.badge)
            ProfileView(client: client).tabItem { Label("Profile", systemImage: "person.crop.circle") }
        }
    }
}
@MainActor
struct RoomListView: View {
    @ObservedObject var client: GameClient
    @State private var selected: JSON = [:]
    @State private var spectator = false
    @State private var showPasskey = false
    @State private var passkey = ""
    private func join(_ room: JSON, watching: Bool) {
        if room.bool("privateRoom") { selected = room; spectator = watching; passkey = ""; showPasskey = true }
        else { client.join(room, spectator: watching) }
    }
    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                StatusView(client: client)
                List {
                    if client.rooms.isEmpty { Text("No rooms yet. Check the master server and refresh.").foregroundStyle(.secondary) }
                    ForEach(Array(client.rooms.enumerated()), id: \.offset) { _, room in
                        VStack(alignment: .leading, spacing: 12) {
                            HStack {
                                Text(room.string("roomName")).font(.headline)
                                if room.bool("privateRoom") { Image(systemName: "lock.fill").accessibilityLabel("Private room") }
                            }
                            Text("\(room.int("players")) / \(room.int("maxPlayers")) players · \(room.int("spectators")) watching").font(.subheadline).foregroundStyle(.secondary)
                            HStack {
                                Button { join(room, watching: false) } label: { Label("Play", systemImage: "play.fill") }.buttonStyle(.borderedProminent)
                                Button { join(room, watching: true) } label: { Label("Watch", systemImage: "eye") }.buttonStyle(.bordered)
                            }.disabled(client.busy)
                        }.padding(.vertical, 6).gtdRow()
                    }
                }.refreshable { client.refreshRooms() }
            }.gtdSurface().navigationTitle("Rooms")
                .toolbar { Button { client.refreshRooms() } label: { Image(systemName: "arrow.clockwise") }.accessibilityLabel("Refresh rooms") }
                .alert("Room passkey", isPresented: $showPasskey) {
                    SecureField("Passkey", text: $passkey)
                    Button("Join") { client.join(selected, spectator: spectator, passkey: passkey); passkey = "" }
                    Button("Cancel", role: .cancel) { passkey = "" }
                }
        }
    }
}
@MainActor
struct PlayerRow: View {
    var profile: JSON
    var detail: String
    var body: some View {
        HStack {
            if !profile.string("countryCode").isEmpty,
               let path = Bundle.main.path(forResource: profile.string("countryCode").lowercased(), ofType: "png", inDirectory: "country-flags"),
               let flag = UIImage(contentsOfFile: path) {
                Image(uiImage: flag).resizable().scaledToFit().frame(width: 28, height: 20)
                    .accessibilityLabel(profile.string("countryCode"))
            }
            VStack(alignment: .leading, spacing: 4) {
                Text(profile.string("displayName")).font(.headline)
                Text(detail).font(.subheadline).foregroundStyle(.secondary)
                let role = profile.bool("masterSuperAdmin") ? "Super-admin" : profile.string("serverRole", "player")
                Text("@\(profile.string("username")) · \(profile.string("countryCode")) · \(role)").font(.caption).foregroundStyle(.secondary)
            }
        }
    }
}
@MainActor
struct LeaderboardView: View {
    @ObservedObject var client: GameClient
    @State private var track = "guessing"
    var body: some View {
        NavigationStack {
            List {
                Picker("Leaderboard", selection: $track) { Text("Guessing").tag("guessing"); Text("Drawing").tag("drawing") }.pickerStyle(.segmented)
                ForEach(Array((client.leaderboards[track] ?? []).prefix(100).enumerated()), id: \.offset) { _, p in
                    InteractivePlayerRow(client: client, social: client.community, profile: p, detail: "#\(p.int(track + "LeaderboardRank")) · \(p.int(track + "Score")) points")
                }
            }.gtdSurface().navigationTitle("Leaderboards").refreshable { client.refreshLeaderboards() }
        }
    }
}
@MainActor
struct ProfileView: View {
    @ObservedObject var client: GameClient
    @State private var name = ""
    @State private var country = ""
    @State private var currentPassword = ""
    @State private var newPassword = ""
    private var countries: [String] {
        let path = Bundle.main.resourceURL?.appendingPathComponent("country-flags")
        return ((try? FileManager.default.contentsOfDirectory(at: path ?? URL(fileURLWithPath: "/invalid"),
            includingPropertiesForKeys: nil)) ?? []).filter { $0.pathExtension == "png" }
            .map { $0.deletingPathExtension().lastPathComponent.uppercased() }.sorted()
    }
    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Text("@\(client.username)").font(.title2.bold())
                    Text("Guessing \(client.profile.int("guessingScore")) · Drawing \(client.profile.int("drawingScore"))")
                    Text("Wins \(client.profile.int("roundsWon")) · Losses \(client.profile.int("roundsLost"))")
                }
                Section("Profile") {
                    TextField("Display name", text: $name)
                    Picker("Country", selection: $country) {
                        Text("No country").tag("")
                        ForEach(countries, id: \.self) { code in HStack { CountryFlag(code: code) }.tag(code) }
                    }
                }
                Section("Change password (optional)") {
                    SecureField("Current password", text: $currentPassword).textContentType(.password)
                    SecureField("New password", text: $newPassword).textContentType(.newPassword)
                }
                Button("Save profile") {
                    client.saveProfile(name: name, country: country, currentPassword: currentPassword, newPassword: newPassword)
                    currentPassword = ""; newPassword = ""
                }.disabled(client.busy)
                StatusView(client: client)
                Button("Sign out", role: .destructive) { client.logout() }
                ThemeMenu()
            }.gtdSurface().navigationTitle("Profile").onAppear { name = client.profile.string("displayName"); country = client.profile.string("countryCode") }
        }
    }
}
@MainActor
struct GameView: View {
    @ObservedObject var client: GameClient
    @StateObject private var tools = DrawingTools()
    @StateObject private var voice = VoiceChat()
    @Environment(\.scenePhase) private var scenePhase
    @State private var tab = 0
    @State private var guess = true
    @State private var message = ""
    @State private var text = ""
    @State private var leave = false
    @State private var clear = false
    @State private var sharedImage: SharedImage?
    @State private var showPeople = false
    @State private var peopleSection = "Friends"
    private var word: String {
        if !client.word.isEmpty { return (client.canDraw ? "Your word: " : "Answer: ") + client.word }
        if client.queued { return "Waiting for a player slot · position \(client.queuePosition)" }
        if client.spectator { return "Spectating" }
        if client.guessed { return "Correct! Watch the rest of the round." }
        return "Watch the drawing and guess!"
    }
    var body: some View {
        NavigationStack {
            VStack(spacing: 8) {
                if client.busy { ProgressView(client.status).font(.caption) }
                else if client.status != client.room.string("roomName"), !client.status.isEmpty {
                    Text(client.status).font(.caption).foregroundStyle(.secondary).lineLimit(2)
                }
                Text(client.roundLabel).font(.subheadline).foregroundStyle(.secondary)
                Text(word).font(.headline).lineLimit(2).minimumScaleFactor(0.7)
                DrawingBoard(client: client, tools: tools).aspectRatio(4 / 3, contentMode: .fit)
                    .background(.white).clipShape(RoundedRectangle(cornerRadius: 12))
                    .overlay(RoundedRectangle(cornerRadius: 12).stroke(.quaternary)).layoutPriority(1)
                if client.canDraw { drawingToolbar }
                if tools.tool == "curve", client.canDraw {
                    HStack {
                        Text(tools.curvePending ? "Drag the line to bend it." : "Drag to place the endpoints.").font(.caption)
                        Spacer()
                        if tools.curvePending {
                            Button("Finish", action: tools.finishCurve).buttonStyle(.borderedProminent)
                            Button("Cancel", action: tools.cancelCurve)
                        }
                    }
                }
                HStack {
                    Button { if voice.recording { voice.stopRecording() } else { voice.requestMicrophone(client: client) } } label: {
                        Label(voice.recording ? "Mic on" : "Mic off", systemImage: voice.recording ? "mic.fill" : "mic.slash")
                    }.disabled(!client.canSpeak && !voice.recording)
                    Spacer()
                    Button { showPeople = true } label: { Label(client.community.badge > 0 ? "People (\(client.community.badge))" : "People", systemImage: "person.2") }
                    Spacer()
                    Button { voice.listening.toggle() } label: {
                        Label(voice.listening ? "Audio on" : "Audio muted", systemImage: voice.listening ? "speaker.wave.2" : "speaker.slash")
                    }
                }.font(.subheadline).buttonStyle(.bordered)
                Picker("Room panel", selection: $tab) { Text("Chat").tag(0); Text("Players").tag(1); Text("Layers").tag(2) }.pickerStyle(.segmented)
                roomPanel.frame(maxHeight: .infinity)
                HStack {
                    Menu { Button("Guess") { guess = true }; Button("Chat") { guess = false } } label: {
                        Label(guess ? "Guess" : "Chat", systemImage: guess ? "lightbulb" : "bubble.left")
                    }
                    TextField(guess ? "Your guess" : "Message", text: $message).textFieldStyle(.roundedBorder)
                        .submitLabel(.send).onSubmit(send).disabled(guess ? !client.canGuess : !client.canTextChat)
                    Button(action: send) { Image(systemName: "arrow.up.circle.fill").font(.title) }
                        .disabled((guess ? !client.canGuess : !client.canTextChat) || message.isEmpty).accessibilityLabel("Send")
                }
            }.padding(.horizontal).padding(.bottom, 8).gtdSurface()
                .navigationTitle(client.room.string("roomName")).navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .navigationBarLeading) { Button("Leave") { leave = true } }
                    ToolbarItem(placement: .navigationBarTrailing) { roomMenu }
                }
                .confirmationDialog("Leave this room?", isPresented: $leave, titleVisibility: .visible) {
                    Button("Leave room", role: .destructive) { voice.close(); client.leaveRoom() }
                }
                .confirmationDialog("Clear all canvas objects?", isPresented: $clear, titleVisibility: .visible) {
                    Button("Clear canvas", role: .destructive) { client.operation(["action": "clear"]) }
                }
                .alert("Add text", isPresented: $tools.askText) {
                    TextField("Text", text: $text)
                    Button("Add") { tools.addText(text); text = "" }; Button("Cancel", role: .cancel) { text = "" }
                }
                .sheet(item: $sharedImage) { ShareSheet(image: $0.image) }
                .sheet(isPresented: $showPeople) { CommunityHub(client: client, initial: peopleSection) }
                .onAppear { voice.attach(client: client); client.voiceReceived = { [weak voice] in voice?.receive($0) } }
                .onDisappear { client.voiceReceived = { _ in }; voice.close() }
                .onChange(of: client.canSpeak) { if !$0 { voice.stopRecording() } }
                .onChange(of: client.connected) { if !$0 { voice.close() } }
                .onChange(of: scenePhase) {
                    if $0 == .background { voice.setForeground(false); tools.cancelCurve() }
                    else if $0 == .active { voice.setForeground(true) }
                }
        }
    }
    private func send() { client.sendText(message, guess: guess); message = "" }
    private var drawingToolbar: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 18) {
                Menu {
                    let names = ["Pen", "Brush", "Airbrush", "Curve", "Polygon", "Line", "Rectangle", "Rounded rectangle", "Ellipse", "Move", "Object eraser", "Fill", "Text"]
                    let types = ["freehand", "brush", "airbrush", "curve", "polygon", "line", "rectangle", "rounded_rectangle", "ellipse", "move", "eraser", "fill", "text"]
                    ForEach(names.indices, id: \.self) { i in Button(names[i]) { tools.tool = types[i] } }
                } label: { Label("Tools", systemImage: "paintbrush.pointed") }
                ColorPicker("Ink", selection: $tools.color, supportsOpacity: false).labelsHidden().frame(width: 32)
                Menu {
                    Picker("Stroke width", selection: $tools.width) { ForEach([2, 4, 6, 12, 20, 32], id: \.self) { Text("\($0)").tag($0) } }
                    Toggle("Fill shapes", isOn: $tools.filled)
                } label: { Label("Size", systemImage: "lineweight") }
                Button(action: tools.undo) { Image(systemName: "arrow.uturn.backward") }.accessibilityLabel("Undo")
                Button(action: tools.redo) { Image(systemName: "arrow.uturn.forward") }.accessibilityLabel("Redo")
            }.frame(minHeight: 44)
        }.disabled(!client.canDraw)
    }
    @ViewBuilder private var roomPanel: some View {
        if tab == 0 {
            ScrollViewReader { reader in
                List(Array(client.messages.enumerated()), id: \.offset) { i, message in Text(message).font(.subheadline).id(i).gtdRow() }
                    .listStyle(.plain).gtdSurface()
                    .onChange(of: client.messages.count) { _ in if !client.messages.isEmpty { reader.scrollTo(client.messages.count - 1, anchor: .bottom) } }
            }
        } else if tab == 1 {
            List(Array(client.players.enumerated()), id: \.offset) { _, p in
                InteractivePlayerRow(client: client, social: client.community, profile: p.object("profile"), detail: (p.bool("spectator") ? "Spectator" : "Player") + " · \(p.int("roomScore")) points")
            }.listStyle(.plain).gtdSurface()
        } else { LayersView(client: client, tools: tools) }
    }
    private var roomMenu: some View {
        Menu {
            ThemeMenu()
            Button("Find players") { peopleSection = "Find players"; showPeople = true }
            Button("Friends & inbox") { peopleSection = "Friends"; showPeople = true }
            if client.community.canModerate { Button("Server moderation") { peopleSection = "Moderation"; showPeople = true } }
            Button(client.queued ? "Cancel player queue" : client.spectator ? "Join as player" : "Switch to spectator") { client.changeRole() }.disabled(client.busy)
            Button(voice.recording ? "Turn microphone off" : "Turn microphone on") {
                if voice.recording { voice.stopRecording() } else { voice.requestMicrophone(client: client) }
            }.disabled(!client.canSpeak)
            Toggle("Listen to room audio", isOn: $voice.listening).onChange(of: voice.listening) { if !$0 { voice.stopPlayback() } }
            Button { if let image = tools.snapshot() { sharedImage = SharedImage(image: image) } } label: { Label("Share drawing", systemImage: "square.and.arrow.up") }
            Button("Clear canvas", role: .destructive) { clear = true }.disabled(!client.canDraw)
        } label: { Image(systemName: "ellipsis.circle") }.accessibilityLabel("Room options")
    }
}
@MainActor
struct LayersView: View {
    @ObservedObject var client: GameClient
    @ObservedObject var tools: DrawingTools
    @State private var name = ""
    @State private var editID = ""
    @State private var showName = false
    private func edit(_ layer: JSON, key: String, value: Any) {
        var layer = layer; layer[key] = value; client.operation(["action": "layer_upsert", "layer": layer])
    }
    private func move(_ id: String, by offset: Int) {
        var ids = client.scene.layers.map { $0.string("id") }
        guard let index = ids.firstIndex(of: id), ids.indices.contains(index + offset) else { return }
        ids.swapAt(index, index + offset); client.operation(["action": "layer_reorder", "layerIds": ids])
    }
    var body: some View {
        List {
            Section("Layers · bottom to top") {
                ForEach(Array(client.scene.layers.enumerated()), id: \.offset) { _, layer in
                    HStack {
                        Button {
                            tools.layerID = layer.string("id")
                        } label: {
                            Label(layer.string("name"), systemImage: tools.layerID == layer.string("id") ? "checkmark.circle.fill" : "circle")
                        }.buttonStyle(.borderless)
                        Spacer()
                        if layer.bool("locked") { Image(systemName: "lock.fill") }
                        if !layer.bool("visible", true) { Image(systemName: "eye.slash") }
                        Menu {
                            Button("Rename") { editID = layer.string("id"); name = layer.string("name"); showName = true }
                            Button(layer.bool("visible", true) ? "Hide" : "Show") { edit(layer, key: "visible", value: !layer.bool("visible", true)) }
                            Button(layer.bool("locked") ? "Unlock" : "Lock") { edit(layer, key: "locked", value: !layer.bool("locked")) }
                            Button("Move up (under)") { move(layer.string("id"), by: -1) }
                            Button("Move down (over)") { move(layer.string("id"), by: 1) }
                            Button("Clear layer") { client.operation(["action": "clear_layer", "layerId": layer.string("id")]) }
                            Button("Delete layer", role: .destructive) { client.operation(["action": "layer_delete", "layerId": layer.string("id")]) }
                        } label: { Image(systemName: "ellipsis") }.disabled(!client.canDraw).accessibilityLabel("Layer actions")
                    }
                }
                Button { editID = ""; name = ""; showName = true } label: { Label("Add layer", systemImage: "plus") }.disabled(!client.canDraw || client.scene.layers.count >= 32)
            }
        }.listStyle(.plain).gtdSurface().alert(editID.isEmpty ? "Add layer" : "Rename layer", isPresented: $showName) {
            TextField("Layer name", text: $name)
            Button("Save") {
                guard !name.trimmingCharacters(in: .whitespaces).isEmpty else { return }
                if let layer = client.scene.layers.first(where: { $0.string("id") == editID }) { edit(layer, key: "name", value: String(name.prefix(32))) }
                else {
                    let id = UUID().uuidString
                    client.operation(["action": "layer_upsert", "layer": ["id": id, "name": String(name.prefix(32)),
                        "order": client.scene.layers.count, "visible": true, "locked": false]])
                    tools.layerID = id
                }
            }
            Button("Cancel", role: .cancel) {}
        }
    }
}
struct SharedImage: Identifiable { let id = UUID(); let image: UIImage }
@MainActor
struct ShareSheet: UIViewControllerRepresentable {
    let image: UIImage
    func makeUIViewController(context: Context) -> UIActivityViewController { UIActivityViewController(activityItems: [image], applicationActivities: nil) }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
