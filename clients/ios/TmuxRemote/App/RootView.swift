import SwiftUI

struct RootView: View {
    let appState: AppState
    @State private var destination: LaunchDestination

    init(appState: AppState) {
        self.appState = appState
        self._destination = State(initialValue: resolveLaunchDestination(
            devices: appState.bookmarkStore.devices,
            lastDeviceId: appState.bookmarkStore.lastDeviceId
        ))
    }

    var body: some View {
        NavigationStack {
            switch destination {
            case .resumeSession(let bookmark, let session):
                TerminalScreen(
                    bookmark: bookmark,
                    sessionName: session,
                    nabtoService: appState.nabtoService,
                    connectionManager: appState.connectionManager,
                    bookmarkStore: appState.bookmarkStore,
                    onDismiss: { destination = .deviceList }
                )
            case .deviceList:
                DeviceListView(
                    nabtoService: appState.nabtoService,
                    connectionManager: appState.connectionManager,
                    bookmarkStore: appState.bookmarkStore
                )
            }
        }
    }
}
