import SwiftUI

@main
struct TmuxRemoteApp: App {
    @State private var appState = AppState()

    var body: some Scene {
        WindowGroup {
            RootView(appState: appState)
        }
    }
}
