import SwiftUI

@main
struct NabtoShellApp: App {
    @State private var appState = AppState()

    var body: some Scene {
        WindowGroup {
            RootView(appState: appState)
        }
    }
}
