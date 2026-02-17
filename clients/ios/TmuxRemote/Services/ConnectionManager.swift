import Foundation
import NabtoEdgeClient

/// Receives connection lifecycle events from the Nabto SDK.
private class EventReceiver: NSObject, ConnectionEventReceiver {
    var onClosed: (() -> Void)?
    private let callbackQueue = DispatchQueue(label: "TmuxRemote.ConnectionEventReceiver")

    func onEvent(event: NabtoEdgeClientConnectionEvent) {
        let onClosedLocal = onClosed
        callbackQueue.async {
            if event == .CLOSED {
                onClosedLocal?()
            }
        }
    }
}

@MainActor
@Observable
class ConnectionManager {
    /// Per-device connection state visible to UI.
    private(set) var deviceStates: [String: ConnectionState] = [:]

    /// Live session lists pushed by agent control streams.
    private(set) var deviceSessions: [String: [SessionInfo]] = [:]

    private struct PendingConnect {
        let id: UUID
        let task: Task<Connection, Error>
    }

    private let client: Client
    private var connections: [String: Connection] = [:]
    private var pendingConnects: [String: PendingConnect] = [:]
    private var eventReceivers: [String: EventReceiver] = [:]
    private var controlStreams: [String: NabtoEdgeClient.Stream] = [:]
    private var controlReadTasks: [String: Task<Void, Never>] = [:]
    private var controlGenerations: [String: UUID] = [:]

    /// Callback for pattern events from agent control stream. (deviceId, event)
    @ObservationIgnored var onPatternEvent: ((String, ControlStreamEvent) -> Void)?

    init() {
        self.client = Client()
    }

    /// Probe terminal sessions for device-list status (CoAP fallback).
    ///
    /// Fast path: use a cached live connection (e.g. when returning from TerminalScreen).
    /// Fallback: use a short-lived isolated connection.
    func probeSessions(for bookmark: DeviceBookmark) async throws -> [SessionInfo] {
        let deviceId = bookmark.deviceId

        if let cached = connections[deviceId] {
            do {
                return try await listSessions(on: cached, timeoutNanoseconds: 2_000_000_000)
            } catch {
                // Drop stale cached connection before isolated fallback.
                disconnect(deviceId: deviceId)
            }
        }

        let probeClient = Client()
        let privateKey = try loadOrCreatePrivateKey(using: probeClient)
        let conn = try probeClient.createConnection()
        try conn.setPrivateKey(key: privateKey)
        try conn.setProductId(id: bookmark.productId)
        try conn.setDeviceId(id: bookmark.deviceId)
        try conn.setServerConnectToken(sct: bookmark.sct)

        defer {
            conn.stop()
        }

        try await connect(conn: conn, timeoutNanoseconds: 10_000_000_000)
        return try await listSessions(on: conn, timeoutNanoseconds: 30_000_000_000)
    }

    nonisolated deinit {
        // deinit is nonisolated; cannot access @MainActor state.
        // Connections will be cleaned up by ARC releasing the NabtoEdgeClient objects.
    }

    /// Returns a cached connected Connection, or creates one.
    /// Serialized per deviceId: concurrent callers for the same device
    /// await the same in-flight Task.
    func connection(for bookmark: DeviceBookmark) async throws -> Connection {
        let deviceId = bookmark.deviceId

        if let existing = connections[deviceId] {
            return existing
        }

        if let pending = pendingConnects[deviceId] {
            return try await pending.task.value
        }

        let pendingId = UUID()
        let task = Task<Connection, Error> { [weak self] in
            guard let self else {
                throw NabtoError.connectionFailed("ConnectionManager deallocated")
            }

            self.deviceStates[deviceId] = .connecting
            let privateKey = try self.loadOrCreatePrivateKey()
            let conn = try self.client.createConnection()
            try conn.setPrivateKey(key: privateKey)
            try conn.setProductId(id: bookmark.productId)
            try conn.setDeviceId(id: bookmark.deviceId)
            try conn.setServerConnectToken(sct: bookmark.sct)
            try await withTaskCancellationHandler {
                try await withThrowingTaskGroup(of: Void.self) { group in
                    group.addTask {
                        try await conn.connectAsync()
                    }
                    group.addTask {
                        try await Task.sleep(nanoseconds: 10_000_000_000)
                        conn.stop()
                        throw NabtoError.connectionFailed("Connection timed out")
                    }
                    // Wait for the first to complete; cancel the other
                    try await group.next()
                    group.cancelAll()
                }
            } onCancel: {
                conn.stop()
            }
            try Task.checkCancellation()

            guard self.pendingConnects[deviceId]?.id == pendingId else {
                conn.stop()
                throw CancellationError()
            }

            let receiver = EventReceiver()
            receiver.onClosed = { [weak self] in
                Task { @MainActor in
                    self?.handleConnectionClosed(deviceId: deviceId)
                }
            }
            try conn.addConnectionEventsReceiver(cb: receiver)

            guard self.pendingConnects[deviceId]?.id == pendingId else {
                conn.removeConnectionEventsReceiver(cb: receiver)
                conn.stop()
                throw CancellationError()
            }

            self.connections[deviceId] = conn
            self.eventReceivers[deviceId] = receiver
            self.pendingConnects.removeValue(forKey: deviceId)
            self.deviceStates[deviceId] = .connected

            // Open control stream for live session updates (best-effort)
            self.openControlStream(deviceId: deviceId, connection: conn)

            return conn
        }

        pendingConnects[deviceId] = PendingConnect(id: pendingId, task: task)

        do {
            return try await task.value
        } catch {
            if pendingConnects[deviceId]?.id == pendingId {
                pendingConnects.removeValue(forKey: deviceId)
                if connections[deviceId] == nil {
                    deviceStates[deviceId] = .disconnected
                }
            }
            throw error
        }
    }

    /// Disconnect and remove a cached connection.
    func disconnect(deviceId: String) {
        if let pending = pendingConnects.removeValue(forKey: deviceId) {
            pending.task.cancel()
        }

        closeControlStream(deviceId: deviceId)

        if let conn = connections.removeValue(forKey: deviceId) {
            if let receiver = eventReceivers.removeValue(forKey: deviceId) {
                conn.removeConnectionEventsReceiver(cb: receiver)
            }
            conn.stop()
        }
        deviceStates[deviceId] = .disconnected
    }

    /// Disconnect all cached and pending connections.
    func disconnectAll() {
        let deviceIds = Set(connections.keys).union(pendingConnects.keys)
        for deviceId in deviceIds {
            disconnect(deviceId: deviceId)
        }
    }

    /// Cancel only the in-flight connect task for a device.
    /// Used when a view is dismissed while an initial resume connect is still running.
    func cancelPendingConnect(deviceId: String) {
        guard let pending = pendingConnects.removeValue(forKey: deviceId) else { return }
        pending.task.cancel()
        if connections[deviceId] == nil {
            deviceStates[deviceId] = .disconnected
        }
    }

    /// Update the connection state for a device (used by NabtoService for reconnect state).
    func setDeviceState(_ state: ConnectionState, for deviceId: String) {
        deviceStates[deviceId] = state
    }

    /// Warm the connection cache on app start.
    func warmCache(bookmarks: [DeviceBookmark]) {
        for bookmark in bookmarks {
            Task {
                _ = try? await connection(for: bookmark)
            }
        }
    }

    /// Create a connection for pairing. Not cached until adoptConnection() is called.
    func connectForPairing(info: PairingInfo) async throws -> Connection {
        deviceStates[info.deviceId] = .connecting
        let privateKey = try loadOrCreatePrivateKey()
        let conn = try client.createConnection()
        try conn.setPrivateKey(key: privateKey)
        try conn.setProductId(id: info.productId)
        try conn.setDeviceId(id: info.deviceId)
        try conn.setServerConnectToken(sct: info.sct)
        try await connect(conn: conn, timeoutNanoseconds: 10_000_000_000)
        deviceStates[info.deviceId] = .connected
        return conn
    }

    /// Adopt an already-open connection into the cache so it is reused for subsequent operations.
    func adoptConnection(_ conn: Connection, for deviceId: String) {
        let receiver = EventReceiver()
        receiver.onClosed = { [weak self] in
            Task { @MainActor in
                self?.handleConnectionClosed(deviceId: deviceId)
            }
        }
        try? conn.addConnectionEventsReceiver(cb: receiver)

        connections[deviceId] = conn
        eventReceivers[deviceId] = receiver
        deviceStates[deviceId] = .connected

        openControlStream(deviceId: deviceId, connection: conn)
    }

    /// Send a pattern resolve message to the agent's control stream.
    func sendPatternResolve(deviceId: String, instanceId: String, decision: String, keys: String? = nil) {
        guard let stream = controlStreams[deviceId] else { return }
        let data = CBORHelpers.encodePatternResolve(instanceId: instanceId, decision: decision, keys: keys)
        Task { try? await stream.writeAsync(data: data) }
    }

    // MARK: - Control Stream

    private func openControlStream(deviceId: String, connection: Connection) {
        closeControlStream(deviceId: deviceId)
        let generation = UUID()
        controlGenerations[deviceId] = generation

        controlReadTasks[deviceId] = Task { [weak self] in
            guard let self else { return }
            do {
                let stream = try connection.createStream()
                try await stream.openAsync(streamPort: 2)
                guard self.controlGenerations[deviceId] == generation else {
                    stream.abort()
                    return
                }
                self.controlStreams[deviceId] = stream
                try await self.controlReadLoop(deviceId: deviceId, stream: stream)
            } catch {
                // Agent may not support control stream (old version); fall back silently.
            }
            if self.controlGenerations[deviceId] == generation {
                self.controlReadTasks.removeValue(forKey: deviceId)
                self.controlStreams.removeValue(forKey: deviceId)
                self.deviceSessions.removeValue(forKey: deviceId)
                self.controlGenerations.removeValue(forKey: deviceId)
            }
        }
    }

    private func closeControlStream(deviceId: String) {
        controlGenerations.removeValue(forKey: deviceId)
        controlReadTasks.removeValue(forKey: deviceId)?.cancel()
        if let stream = controlStreams.removeValue(forKey: deviceId) {
            stream.abort()
        }
        deviceSessions.removeValue(forKey: deviceId)
    }

    private func controlReadLoop(deviceId: String, stream: NabtoEdgeClient.Stream) async throws {
        var readBuffer = Data()

        while !Task.isCancelled {
            // Read 4-byte big-endian length prefix
            let lengthData = try await readExactly(stream: stream, buffer: &readBuffer, count: 4)
            let bytes = [UInt8](lengthData)
            let length =
                (UInt32(bytes[0]) << 24) |
                (UInt32(bytes[1]) << 16) |
                (UInt32(bytes[2]) << 8)  |
                UInt32(bytes[3])

            guard length > 0 && length < 65536 else { break }

            // Read CBOR payload
            let payload = try await readExactly(stream: stream, buffer: &readBuffer, count: Int(length))

            guard let event = CBORHelpers.decodeControlStreamEvent(from: payload) else { continue }

            switch event {
            case .sessions(let sessions):
                self.deviceSessions[deviceId] = sessions
            case .patternPresent(let match):
                AppLog.log("controlReadLoop: decoded patternPresent instance=%@", match.id)
                let hasCallback = self.onPatternEvent != nil
                AppLog.log("controlReadLoop: patternPresent instance=%@, onPatternEvent=%@",
                           match.id, hasCallback ? "set" : "NIL")
                self.onPatternEvent?(deviceId, event)
            case .patternUpdate(let match):
                AppLog.log("controlReadLoop: decoded patternUpdate instance=%@", match.id)
                let hasCallback = self.onPatternEvent != nil
                AppLog.log("controlReadLoop: patternUpdate instance=%@, onPatternEvent=%@",
                           match.id, hasCallback ? "set" : "NIL")
                self.onPatternEvent?(deviceId, event)
            case .patternGone(let instanceId):
                AppLog.log("controlReadLoop: decoded patternGone instance=%@", instanceId)
                let hasCallback = self.onPatternEvent != nil
                AppLog.log("controlReadLoop: patternGone instance=%@, onPatternEvent=%@",
                           instanceId,
                           hasCallback ? "set" : "NIL")
                self.onPatternEvent?(deviceId, event)
            }
        }
    }

    /// Read exactly `count` bytes from a Nabto stream, preserving excess
    /// bytes in `buffer` for subsequent calls.
    private func readExactly(stream: NabtoEdgeClient.Stream, buffer: inout Data, count: Int) async throws -> Data {
        while buffer.count < count {
            try Task.checkCancellation()
            let chunk = try await stream.readSomeAsync()
            if chunk.isEmpty { throw NabtoError.streamFailed("Control stream closed") }
            buffer.append(chunk)
        }
        let result = buffer.prefix(count)
        buffer.removeFirst(count)
        return Data(result)
    }

    private func handleConnectionClosed(deviceId: String) {
        closeControlStream(deviceId: deviceId)
        connections.removeValue(forKey: deviceId)
        eventReceivers.removeValue(forKey: deviceId)
        deviceStates[deviceId] = .disconnected
    }

    private func connect(conn: Connection, timeoutNanoseconds: UInt64) async throws {
        try await withTaskCancellationHandler {
            try await withThrowingTaskGroup(of: Void.self) { group in
                group.addTask {
                    try await conn.connectAsync()
                }
                group.addTask {
                    try await Task.sleep(nanoseconds: timeoutNanoseconds)
                    conn.stop()
                    throw NabtoError.connectionFailed("Probe connection timed out")
                }
                try await group.next()
                group.cancelAll()
            }
        } onCancel: {
            conn.stop()
        }
    }

    /// CoAP-based session list (fallback for old agents without control stream).
    func listSessionsCoAP(on conn: Connection, timeoutNanoseconds: UInt64) async throws -> [SessionInfo] {
        return try await listSessions(on: conn, timeoutNanoseconds: timeoutNanoseconds)
    }

    private func listSessions(on conn: Connection, timeoutNanoseconds: UInt64) async throws -> [SessionInfo] {
        let coap = try conn.createCoapRequest(method: "GET", path: "/terminal/sessions")
        let response = try await executeCoap(coap: coap, timeoutNanoseconds: timeoutNanoseconds)

        guard response.status == 205 else {
            throw NabtoError.coapFailed("List sessions", response.status)
        }
        guard let payload = response.payload else {
            return []
        }
        return CBORHelpers.decodeSessions(from: payload)
    }

    private func executeCoap(coap: CoapRequest, timeoutNanoseconds: UInt64) async throws -> CoapResponse {
        return try await withTaskCancellationHandler {
            try await withThrowingTaskGroup(of: CoapResponse.self) { group in
                group.addTask {
                    try await coap.executeAsync()
                }
                group.addTask {
                    try await Task.sleep(nanoseconds: timeoutNanoseconds)
                    coap.stop()
                    throw NabtoError.connectionFailed("CoAP request timed out")
                }
                guard let first = try await group.next() else {
                    throw NabtoError.connectionFailed("CoAP request returned no result")
                }
                group.cancelAll()
                return first
            }
        } onCancel: {
            coap.stop()
        }
    }

    private func loadOrCreatePrivateKey(using keyClient: Client? = nil) throws -> String {
        if let existing = KeychainService.loadPrivateKey() {
            return existing
        }
        let key = try (keyClient ?? client).createPrivateKey()
        guard KeychainService.savePrivateKey(key) else {
            throw NabtoError.connectionFailed("Failed to save private key to Keychain")
        }
        return key
    }
}
