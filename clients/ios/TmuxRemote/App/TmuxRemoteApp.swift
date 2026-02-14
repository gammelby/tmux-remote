import SwiftUI

@main
struct TmuxRemoteApp: App {
    @State private var appState = AppState()
    @AppStorage("tmuxremote_theme") private var theme: AppTheme = .dark

    var body: some Scene {
        WindowGroup {
            RootView(appState: appState)
                .preferredColorScheme(theme.colorScheme)
        }
    }
}
