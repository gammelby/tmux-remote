import SwiftUI

struct TerminalScreen: View {
    let bookmark: DeviceBookmark
    let sessionName: String
    let nabtoService: NabtoService
    let connectionManager: ConnectionManager
    let bookmarkStore: BookmarkStore

    @State private var bridge = TerminalBridge()
    @State private var currentCols: Int = 80
    @State private var currentRows: Int = 24
    @State private var errorMessage: String?
    @State private var showError = false
    @State private var isTerminalReady = false
    @State private var initialConnectionDone = false
    @State private var isConnecting = false
    @State private var isReconnecting = false
    @State private var isDismissing = false
    @Environment(\.scenePhase) private var scenePhase
    let onDismiss: () -> Void

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
                    guard initialConnectionDone else { return }
                    Task {
                        await nabtoService.resize(bookmark: bookmark, cols: cols, rows: rows)
                    }
                },
                onReady: {
                    setupCallbacks()
                    isTerminalReady = true
                }
            )
            .ignoresSafeArea(.container, edges: .bottom)
            .accessibilityIdentifier("terminal-view")

            VStack {
                HStack {
                    Button {
                        dismissToDevices()
                    } label: {
                        HStack(spacing: 4) {
                            Image(systemName: "chevron.left")
                            Text("Devices")
                        }
                        .font(.caption)
                        .fontWeight(.medium)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(Color.gray.opacity(0.6))
                        .foregroundColor(.white)
                        .clipShape(Capsule())
                    }
                    .accessibilityIdentifier("back-to-devices")
                    .padding(.leading, 12)
                    .padding(.top, 8)

                    Spacer()

                    connectionPill
                        .padding(.trailing, 12)
                        .padding(.top, 8)
                }
                Spacer()
            }

            if isReconnecting {
                reconnectOverlay
                    .accessibilityIdentifier("reconnect-overlay")
            }
        }
        .navigationBarHidden(true)
        .task(id: isTerminalReady) {
            guard isTerminalReady, !isConnecting, !initialConnectionDone else { return }
            isConnecting = true
            await connectAndAttach()
            initialConnectionDone = true
            isConnecting = false
        }
        .onChange(of: scenePhase) { _, newPhase in
            if newPhase == .active && initialConnectionDone {
                handleForegroundReturn()
            }
        }
        .onChange(of: connectionManager.deviceStates[bookmark.deviceId]) { _, newState in
            if newState == .disconnected && initialConnectionDone && !isReconnecting && !isDismissing {
                handleStreamClosed()
            }
        }
        .alert("Error", isPresented: $showError) {
            Button("Retry") {
                Task { await connectAndAttach() }
            }
            Button("Back", role: .cancel) {
                dismissToDevices()
            }
        } message: {
            Text(errorMessage ?? "Unknown error")
        }
    }

    @ViewBuilder
    private var connectionPill: some View {
        let state = connectionManager.deviceStates[bookmark.deviceId] ?? .disconnected
        let (text, color) = pillContent(for: state)
        Text(text)
            .font(.caption2)
            .fontWeight(.medium)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(color.opacity(0.85))
            .foregroundColor(.white)
            .clipShape(Capsule())
            .accessibilityIdentifier("connection-pill")
    }

    private func pillContent(for state: ConnectionState) -> (String, Color) {
        switch state {
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

    private func dismissToDevices() {
        isDismissing = true
        nabtoService.disconnect()
        onDismiss()
    }

    private func setupCallbacks() {
        nabtoService.onStreamData = { [bridge] bytes in
            bridge.feed(bytes: bytes)
        }
        nabtoService.onStreamClosed = { [self] in
            guard !isDismissing else { return }
            handleStreamClosed()
        }
    }

    private func connectAndAttach() async {
        do {
            try await nabtoService.connect(bookmark: bookmark)
            try await nabtoService.attach(bookmark: bookmark, session: sessionName, cols: currentCols, rows: currentRows)
            try await nabtoService.openStream(bookmark: bookmark)
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

    @ViewBuilder
    private var reconnectOverlay: some View {
        ZStack {
            Color.black.opacity(0.7)
                .ignoresSafeArea()

            VStack(spacing: 20) {
                ProgressView()
                    .progressViewStyle(CircularProgressViewStyle(tint: .white))
                    .scaleEffect(1.5)

                Text("Reconnecting...")
                    .font(.headline)
                    .foregroundColor(.white)

                Button {
                    dismissToDevices()
                } label: {
                    Text("Back to Devices")
                        .font(.subheadline)
                        .fontWeight(.medium)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 8)
                        .background(Color.gray.opacity(0.6))
                        .foregroundColor(.white)
                        .clipShape(Capsule())
                }
            }
        }
    }

    private func handleForegroundReturn() {
        let state = connectionManager.deviceStates[bookmark.deviceId] ?? .disconnected
        guard state != .connected || nabtoService.currentSession == nil else {
            return
        }
        isReconnecting = true
        nabtoService.attemptReconnect(
            bookmark: bookmark,
            session: sessionName,
            cols: currentCols,
            rows: currentRows,
            onSuccess: {
                isReconnecting = false
            },
            onGiveUp: {
                isReconnecting = false
                dismissToDevices()
            }
        )
    }

    private func handleStreamClosed() {
        guard !isReconnecting else { return }
        isReconnecting = true
        nabtoService.attemptReconnect(
            bookmark: bookmark,
            session: sessionName,
            cols: currentCols,
            rows: currentRows,
            onSuccess: {
                isReconnecting = false
            },
            onGiveUp: {
                isReconnecting = false
                dismissToDevices()
            }
        )
    }
}
