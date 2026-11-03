import SwiftUI

@main
@MainActor
struct GuessTheDrawingApp: App {
    @StateObject private var client = GameClient()
    var body: some Scene {
        WindowGroup { RootView(client: client) }
    }
}
