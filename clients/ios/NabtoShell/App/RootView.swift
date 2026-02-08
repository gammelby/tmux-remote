import SwiftUI

struct RootView: View {
    let appState: AppState

    var body: some View {
        NavigationStack {
            switch resolveLaunchDestination(
                devices: appState.bookmarkStore.devices,
                lastDeviceId: appState.bookmarkStore.lastDeviceId
            ) {
            case .resumeSession(let bookmark, let session):
                TerminalScreen(
                    bookmark: bookmark,
                    sessionName: session,
                    nabtoService: appState.nabtoService,
                    bookmarkStore: appState.bookmarkStore
                )
            case .deviceList:
                DeviceListView(
                    nabtoService: appState.nabtoService,
                    bookmarkStore: appState.bookmarkStore
                )
            }
        }
    }
}
