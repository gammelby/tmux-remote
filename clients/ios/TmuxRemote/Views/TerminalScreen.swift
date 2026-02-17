import SwiftUI

struct TerminalScreen: View {
    let bookmark: DeviceBookmark
    let sessionName: String
    let nabtoService: NabtoService
    let connectionManager: ConnectionManager
    let bookmarkStore: BookmarkStore

    @State private var bridge = TerminalBridge()
    @State private var patternEngine = PatternEngine()
    @State private var currentCols: Int = 80
    @State private var currentRows: Int = 24
    @State private var errorMessage: String?
    @State private var showError = false
    @State private var isTerminalReady = false
    @State private var initialConnectionDone = false
    @State private var isConnecting = false
    @State private var isReconnecting = false
    @State private var isDismissing = false
    @State private var overlayOffset: CGSize = .zero
    @State private var overlayDragBase: CGSize = .zero
    @State private var connectTask: Task<Void, Never>?
    #if DEBUG
    @State private var lastSentKeys: String = ""
    #endif
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
                    guard !isDismissing else { return }
                    setupCallbacks()
                    isTerminalReady = true
                }
            )
            .ignoresSafeArea(.container, edges: .bottom)
            .allowsHitTesting(patternEngine.visibleMatch == nil)
            .accessibilityIdentifier("terminal-view")

            VStack {
                HStack {
                    Button {
                        dismissToDevices()
                    } label: {
                        HStack(spacing: 4) {
                            Image(systemName: "chevron.left")
                            Text("Agents")
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

                    if patternEngine.canRestoreHiddenMatch {
                        Button {
                            patternEngine.restoreOverlay()
                        } label: {
                            HStack(spacing: 4) {
                                Image(systemName: "arrow.clockwise")
                                Text("Show prompt")
                            }
                            .font(.caption)
                            .fontWeight(.medium)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(Color.orange.opacity(0.85))
                            .foregroundColor(.white)
                            .clipShape(Capsule())
                        }
                        .accessibilityIdentifier("pattern-recall-pill")
                        .padding(.top, 8)
                    }

                    connectionPill
                        .padding(.trailing, 12)
                        .padding(.top, 8)
                }
                Spacer()
            }

            if let match = patternEngine.visibleMatch {
                Color.black.opacity(0.001)
                    .ignoresSafeArea()
                    .accessibilityIdentifier("pattern-overlay-backdrop")
                VStack {
                    Spacer()
                    PatternOverlayView(
                        match: match,
                        onAction: { action in
                            #if DEBUG
                            lastSentKeys = action.keys
                            #endif
                            nabtoService.writeToStream(Data(action.keys.utf8))
                            patternEngine.resolveLocally(instanceId: match.id)
                            connectionManager.sendPatternResolve(
                                deviceId: bookmark.deviceId,
                                instanceId: match.id,
                                decision: "action",
                                keys: action.keys
                            )
                            overlayOffset = .zero
                            overlayDragBase = .zero
                        },
                        onDismiss: {
                            patternEngine.dismissLocally(instanceId: match.id)
                            overlayOffset = .zero
                            overlayDragBase = .zero
                        }
                    )
                }
                .offset(x: overlayOffset.width, y: overlayOffset.height)
                .gesture(
                    DragGesture()
                        .onChanged { value in
                            overlayOffset = CGSize(
                                width: overlayDragBase.width + value.translation.width,
                                height: overlayDragBase.height + value.translation.height
                            )
                        }
                        .onEnded { _ in
                            overlayDragBase = overlayOffset
                        }
                )
                .transition(.move(edge: .bottom))
                .animation(.easeInOut(duration: 0.2), value: patternEngine.visibleMatch != nil)
            }

            #if DEBUG
            Text(lastSentKeys)
                .frame(width: 0, height: 0)
                .opacity(0)
                .accessibilityIdentifier("debug-sent-keys")
            Text("match:\(patternEngine.activeMatch?.id ?? "nil") hidden:\(patternEngine.canRestoreHiddenMatch) err:\(showError) done:\(initialConnectionDone)")
                .frame(width: 0, height: 0)
                .opacity(0)
                .accessibilityIdentifier("debug-pattern-state")
            #endif

            if isReconnecting {
                reconnectOverlay
                    .accessibilityIdentifier("reconnect-overlay")
            }
        }
        .navigationBarHidden(true)
        .task(id: isTerminalReady) {
            guard isTerminalReady, !isConnecting, !initialConnectionDone, !isDismissing else { return }
            isConnecting = true
            let task = Task {
                await connectAndAttach()
            }
            connectTask = task
            await task.value
            if !task.isCancelled && !isDismissing {
                initialConnectionDone = true
            }
            connectTask = nil
            isConnecting = false
        }
        .onDisappear {
            isDismissing = true
            connectionManager.onPatternEvent = nil
            teardownTransientTasks()
            nabtoService.disableReconnectContext()
            nabtoService.disconnect(keepConnection: true)
        }
        .onChange(of: scenePhase) { _, newPhase in
            switch newPhase {
            case .background:
                nabtoService.closeStream()
            case .active:
                if initialConnectionDone {
                    handleForegroundReturn()
                }
            default:
                break
            }
        }
        .onChange(of: connectionManager.deviceStates[bookmark.deviceId]) { _, newState in
            if newState == .disconnected &&
                initialConnectionDone &&
                !isReconnecting &&
                !isDismissing &&
                scenePhase == .active &&
                nabtoService.canAutoReconnect(deviceId: bookmark.deviceId, session: sessionName) {
                handleStreamClosed()
            }
        }
        .onChange(of: patternEngine.activeMatch?.id) { _, _ in
            overlayOffset = .zero
            overlayDragBase = .zero
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
            return ("Connecting...", .tmuxAccent)
        case .connected:
            return ("Connected", .tmuxOnline)
        case .reconnecting(let attempt):
            return ("Reconnecting (\(attempt))...", .orange)
        case .offline:
            return ("Offline", .tmuxOffline)
        }
    }

    private func dismissToDevices() {
        isDismissing = true
        connectionManager.onPatternEvent = nil
        teardownTransientTasks()
        nabtoService.disableReconnectContext()
        nabtoService.disconnect(keepConnection: true)
        onDismiss()
    }

    private func teardownTransientTasks() {
        connectTask?.cancel()
        connectTask = nil
        isConnecting = false
    }

    private func setupCallbacks() {
        nabtoService.enableReconnectContext(deviceId: bookmark.deviceId, session: sessionName)
        nabtoService.onStreamData = { [bridge] bytes in
            bridge.feed(bytes: bytes)
        }
        nabtoService.onStreamClosed = { [self] in
            guard nabtoService.canAutoReconnect(deviceId: bookmark.deviceId, session: sessionName) else { return }
            handleStreamClosed()
        }
        connectionManager.onPatternEvent = { [weak patternEngine] deviceId, event in
            guard deviceId == bookmark.deviceId else { return }
            switch event {
            case .patternPresent(let match):
                patternEngine?.applyServerPresent(match)
            case .patternUpdate(let match):
                patternEngine?.applyServerUpdate(match)
            case .patternGone(let instanceId):
                patternEngine?.applyServerGone(instanceId: instanceId)
            case .sessions:
                break
            }
        }
    }

    private func connectAndAttach() async {
        guard !isDismissing else { return }
        do {
            try await nabtoService.connect(bookmark: bookmark)
            try Task.checkCancellation()
            guard !isDismissing else { throw CancellationError() }
            try await nabtoService.attach(bookmark: bookmark, session: sessionName, cols: currentCols, rows: currentRows)
            try Task.checkCancellation()
            guard !isDismissing else { throw CancellationError() }
            try await nabtoService.openStream(bookmark: bookmark)
            try Task.checkCancellation()
            guard !isDismissing else { throw CancellationError() }
            bookmarkStore.updateLastSession(deviceId: bookmark.deviceId, session: sessionName)
        } catch is CancellationError {
            return
        } catch let error as NabtoError {
            switch error {
            case .sessionNotFound:
                bookmarkStore.clearLastSession(deviceId: bookmark.deviceId)
                dismissToDevices()
                return
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
                    Text("Back to Agents")
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
        guard nabtoService.canAutoReconnect(deviceId: bookmark.deviceId, session: sessionName) else {
            return
        }
        let state = connectionManager.deviceStates[bookmark.deviceId] ?? .disconnected
        guard state != .connected || nabtoService.currentSession == nil else {
            return
        }
        patternEngine.reset()
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
        guard nabtoService.canAutoReconnect(deviceId: bookmark.deviceId, session: sessionName) else {
            return
        }
        guard !isReconnecting else { return }
        patternEngine.reset()
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
