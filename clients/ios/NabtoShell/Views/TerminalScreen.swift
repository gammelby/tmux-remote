import SwiftUI

struct TerminalScreen: View {
    let bookmark: DeviceBookmark
    let sessionName: String
    let nabtoService: NabtoService
    let bookmarkStore: BookmarkStore

    @State private var bridge = TerminalBridge()
    @State private var currentCols: Int = 80
    @State private var currentRows: Int = 24
    @State private var errorMessage: String?
    @State private var showError = false
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            TerminalViewWrapper(
                bridge: bridge,
                onSend: { data in
                    nabtoService.writeToStream(data)
                },
                onSizeChanged: { cols, rows in
                    currentCols = cols
                    currentRows = rows
                    Task {
                        await nabtoService.resize(cols: cols, rows: rows)
                    }
                }
            )
            .ignoresSafeArea(.container, edges: .bottom)
            .onAppear { setupCallbacks() }

            // Connection status pill
            VStack {
                HStack {
                    Spacer()
                    connectionPill
                        .padding(.trailing, 12)
                        .padding(.top, 8)
                }
                Spacer()
            }
        }
        .navigationBarHidden(true)
        .task { await connectAndAttach() }
        .onChange(of: scenePhase) { _, newPhase in
            if newPhase == .active {
                handleForegroundReturn()
            }
        }
        .alert("Error", isPresented: $showError) {
            Button("Retry") {
                Task { await connectAndAttach() }
            }
            Button("Back", role: .cancel) {
                dismiss()
            }
        } message: {
            Text(errorMessage ?? "Unknown error")
        }
    }

    @ViewBuilder
    private var connectionPill: some View {
        let (text, color) = pillContent
        Text(text)
            .font(.caption2)
            .fontWeight(.medium)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(color.opacity(0.85))
            .foregroundColor(.white)
            .clipShape(Capsule())
    }

    private var pillContent: (String, Color) {
        switch nabtoService.connectionState {
        case .disconnected:
            return ("Disconnected", .gray)
        case .connecting:
            return ("Connecting...", .blue)
        case .connected:
            return ("Connected", .green)
        case .reconnecting(let attempt):
            return ("Reconnecting (\(attempt))...", .orange)
        case .offline:
            return ("Offline", .red)
        }
    }

    private func setupCallbacks() {
        nabtoService.onStreamData = { [bridge] bytes in
            bridge.feed(bytes: bytes)
        }
        nabtoService.onStreamClosed = {
            handleStreamClosed()
        }
    }

    private func connectAndAttach() async {
        do {
            if nabtoService.connectionState != .connected {
                try await nabtoService.connect(bookmark: bookmark)
            }
            try await nabtoService.attach(session: sessionName, cols: currentCols, rows: currentRows)
            try await nabtoService.openStream()
            bookmarkStore.updateLastSession(deviceId: bookmark.deviceId, session: sessionName)
        } catch let error as NabtoError {
            switch error {
            case .sessionNotFound(let name):
                errorMessage = "Session '\(name)' no longer exists."
                showError = true
            default:
                errorMessage = error.localizedDescription
                showError = true
            }
        } catch {
            errorMessage = error.localizedDescription
            showError = true
        }
    }

    private func handleForegroundReturn() {
        guard nabtoService.connectionState != .connected || nabtoService.currentSession == nil else {
            return
        }
        nabtoService.attemptReconnect(
            bookmark: bookmark,
            session: sessionName,
            cols: currentCols,
            rows: currentRows
        )
    }

    private func handleStreamClosed() {
        nabtoService.attemptReconnect(
            bookmark: bookmark,
            session: sessionName,
            cols: currentCols,
            rows: currentRows
        )
    }
}
