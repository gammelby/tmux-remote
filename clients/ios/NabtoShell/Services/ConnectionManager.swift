import Foundation
import NabtoEdgeClient

/// Receives connection lifecycle events from the Nabto SDK.
private class EventReceiver: NSObject, ConnectionEventReceiver {
    var onClosed: (() -> Void)?

    func onEvent(event: NabtoEdgeClientConnectionEvent) {
        if event == .CLOSED {
            onClosed?()
        }
    }
}

@Observable
class ConnectionManager {
    /// Per-device connection state visible to UI.
    private(set) var deviceStates: [String: ConnectionState] = [:]

    private let client: Client
    private var connections: [String: Connection] = [:]
    private var pendingConnects: [String: Task<Connection, Error>] = [:]
    private var eventReceivers: [String: EventReceiver] = [:]

    init() {
        self.client = Client()
    }

    deinit {
        for (deviceId, _) in connections {
            disconnect(deviceId: deviceId)
        }
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
            return try await pending.value
        }

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
            try await conn.connectAsync()

            let receiver = EventReceiver()
            receiver.onClosed = { [weak self] in
                Task { @MainActor in
                    self?.handleConnectionClosed(deviceId: deviceId)
                }
            }
            try conn.addConnectionEventsReceiver(cb: receiver)

            self.connections[deviceId] = conn
            self.eventReceivers[deviceId] = receiver
            self.pendingConnects.removeValue(forKey: deviceId)
            self.deviceStates[deviceId] = .connected
            return conn
        }

        pendingConnects[deviceId] = task

        do {
            return try await task.value
        } catch {
            pendingConnects.removeValue(forKey: deviceId)
            deviceStates[deviceId] = .disconnected
            throw error
        }
    }

    /// Disconnect and remove a cached connection.
    func disconnect(deviceId: String) {
        pendingConnects[deviceId]?.cancel()
        pendingConnects.removeValue(forKey: deviceId)

        if let conn = connections.removeValue(forKey: deviceId) {
            if let receiver = eventReceivers.removeValue(forKey: deviceId) {
                conn.removeConnectionEventsReceiver(cb: receiver)
            }
            conn.stop()
        }
        deviceStates[deviceId] = .disconnected
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
        try await conn.connectAsync()
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
    }

    private func handleConnectionClosed(deviceId: String) {
        connections.removeValue(forKey: deviceId)
        eventReceivers.removeValue(forKey: deviceId)
        deviceStates[deviceId] = .disconnected
    }

    private func loadOrCreatePrivateKey() throws -> String {
        if let existing = KeychainService.loadPrivateKey() {
            return existing
        }
        let key = try client.createPrivateKey()
        guard KeychainService.savePrivateKey(key) else {
            throw NabtoError.connectionFailed("Failed to save private key to Keychain")
        }
        return key
    }
}
