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
            .allowsHitTesting(patternEngine.activeMatch == nil)
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

                    agentPill
                        .padding(.top, 8)

                    recallPill
                        .padding(.top, 8)

                    connectionPill
                        .padding(.trailing, 12)
                        .padding(.top, 8)
                }
                Spacer()
            }

            if let match = patternEngine.activeMatch {
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
                            overlayOffset = .zero
                            overlayDragBase = .zero
                            patternEngine.consume()
                            // No sendPatternDismiss here: the keystroke resolves the
                            // prompt and the agent auto-dismisses. Sending an explicit
                            // dismiss would trigger a 4000-char agent-side cooldown
                            // that suppresses detection of the next prompt.
                        },
                        onDismiss: {
                            overlayOffset = .zero
                            overlayDragBase = .zero
                            let wasRecalled = patternEngine.isRecalledMatch
                            patternEngine.dismiss()
                            if !wasRecalled {
                                connectionManager.sendPatternDismiss(deviceId: bookmark.deviceId)
                            }
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
                .animation(.easeInOut(duration: 0.2), value: patternEngine.activeMatch != nil)
            }

            #if DEBUG
            Text(lastSentKeys)
                .frame(width: 0, height: 0)
                .opacity(0)
                .accessibilityIdentifier("debug-sent-keys")
            Text("agent:\(patternEngine.activeAgent ?? "nil") match:\(patternEngine.activeMatch?.id ?? "nil") last:\(patternEngine.lastMatch?.id ?? "nil") err:\(showError) done:\(initialConnectionDone)")
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
            if let config = PatternConfigLoader.loadBundled() {
                patternEngine.loadConfig(config)
            }
            patternEngine.setDeviceId(bookmark.deviceId)
            #if DEBUG
            if let idx = ProcessInfo.processInfo.arguments.firstIndex(of: "--stub-agent"),
               idx + 1 < ProcessInfo.processInfo.arguments.count {
                patternEngine.selectAgent(ProcessInfo.processInfo.arguments[idx + 1])
            }
            #endif
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
            if newPhase == .active && initialConnectionDone {
                handleForegroundReturn()
            }
        }
        .onChange(of: connectionManager.deviceStates[bookmark.deviceId]) { _, newState in
            if newState == .disconnected &&
                initialConnectionDone &&
                !isReconnecting &&
                !isDismissing &&
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
    private var agentPill: some View {
        Menu {
            Button(patternEngine.activeAgent == nil ? "Off (selected)" : "Off") {
                patternEngine.selectAgent(nil)
            }
            ForEach(patternEngine.availableAgents, id: \.id) { agent in
                let selected = patternEngine.activeAgent == agent.id
                Button(selected ? "\(agent.name) (selected)" : agent.name) {
                    patternEngine.selectAgent(agent.id)
                }
            }
        } label: {
            HStack(spacing: 4) {
                Image(systemName: "wand.and.stars")
                Text(agentPillLabel)
            }
            .font(.caption2)
            .fontWeight(.medium)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(Color.purple.opacity(patternEngine.activeAgent != nil ? 0.85 : 0.4))
            .foregroundColor(.white)
            .clipShape(Capsule())
        }
        .accessibilityIdentifier("agent-pill")
    }

    @ViewBuilder
    private var recallPill: some View {
        if patternEngine.activeMatch == nil && patternEngine.lastMatch != nil {
            Button {
                patternEngine.recall()
            } label: {
                Image(systemName: "arrow.uturn.backward")
                    .font(.caption2)
                    .fontWeight(.medium)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(Color.orange.opacity(0.85))
                    .foregroundColor(.white)
                    .clipShape(Capsule())
            }
            .accessibilityIdentifier("recall-pill")
            .transition(.scale.combined(with: .opacity))
            .animation(.easeInOut(duration: 0.2), value: patternEngine.lastMatch != nil)
        }
    }

    private var agentPillLabel: String {
        guard let agentId = patternEngine.activeAgent,
              let config = patternEngine.availableAgents.first(where: { $0.id == agentId }) else {
            return "Agent"
        }
        return config.name
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
            let isMyDevice = deviceId == bookmark.deviceId
            switch event {
            case .patternMatch(let match):
                AppLog.log("onPatternEvent: patternMatch id=%@, deviceMatch=%d, engineAlive=%d",
                           match.id, isMyDevice ? 1 : 0, patternEngine != nil ? 1 : 0)
            case .patternDismiss:
                AppLog.log("onPatternEvent: patternDismiss, deviceMatch=%d, engineAlive=%d",
                           isMyDevice ? 1 : 0, patternEngine != nil ? 1 : 0)
            case .sessions:
                break
            }
            guard isMyDevice else { return }
            switch event {
            case .patternMatch(let match):
                patternEngine?.applyServerMatch(match)
            case .patternDismiss:
                patternEngine?.applyServerDismiss()
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
            // Navigated away before initial resume connect completed.
            return
        } catch let error as NabtoError {
            switch error {
            case .sessionNotFound:
                // Session was destroyed; silently return to device list.
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
