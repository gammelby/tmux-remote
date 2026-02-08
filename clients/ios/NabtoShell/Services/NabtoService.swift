import Foundation
import NabtoEdgeClient
import SwiftCBOR

enum ConnectionState: Equatable {
    case disconnected
    case connecting
    case connected
    case reconnecting(attempt: Int)
    case offline
}

enum NabtoError: Error, LocalizedError {
    case connectionFailed(String)
    case pairingFailed(String)
    case coapFailed(String, UInt16)
    case streamFailed(String)
    case sessionNotFound(String)
    case alreadyPaired

    var errorDescription: String? {
        switch self {
        case .connectionFailed(let msg): return "Connection failed: \(msg)"
        case .pairingFailed(let msg): return "Pairing failed: \(msg)"
        case .coapFailed(let msg, let code): return "CoAP error \(code): \(msg)"
        case .streamFailed(let msg): return "Stream error: \(msg)"
        case .sessionNotFound(let name): return "Session '\(name)' not found"
        case .alreadyPaired: return "Already paired with this device"
        }
    }
}

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
class NabtoService {
    private(set) var connectionState: ConnectionState = .disconnected
    private(set) var currentDeviceId: String?
    private(set) var currentSession: String?

    private var client: Client?
    private var connection: Connection?
    private var stream: NabtoEdgeClient.Stream?
    private var readTask: Task<Void, Never>?
    private var reconnectTask: Task<Void, Never>?
    private var eventReceiver: EventReceiver?

    /// Called on main actor when stream data arrives. Set by TerminalScreen.
    var onStreamData: (([UInt8]) -> Void)?

    /// Called on main actor when the stream ends unexpectedly.
    var onStreamClosed: (() -> Void)?

    private let bookmarkStore: BookmarkStore

    private let reconnectLogic = ReconnectLogic()

    init(bookmarkStore: BookmarkStore) {
        self.bookmarkStore = bookmarkStore
    }

    deinit {
        disconnect()
    }

    // MARK: - Private Key

    private func loadOrCreatePrivateKey() throws -> String {
        if let existing = KeychainService.loadPrivateKey() {
            return existing
        }
        guard let client = client else {
            throw NabtoError.connectionFailed("Client not initialized")
        }
        let key = try client.createPrivateKey()
        guard KeychainService.savePrivateKey(key) else {
            throw NabtoError.connectionFailed("Failed to save private key to Keychain")
        }
        return key
    }

    // MARK: - Connection

    func connect(bookmark: DeviceBookmark) async throws {
        disconnect()

        connectionState = .connecting
        currentDeviceId = bookmark.deviceId

        let c = Client()
        self.client = c

        let privateKey = try loadOrCreatePrivateKey()

        let conn = try c.createConnection()
        self.connection = conn

        try conn.setPrivateKey(key: privateKey)
        try conn.setProductId(id: bookmark.productId)
        try conn.setDeviceId(id: bookmark.deviceId)
        try conn.setServerConnectToken(sct: bookmark.sct)

        try await conn.connectAsync()

        // Listen for connection close events
        let receiver = EventReceiver()
        receiver.onClosed = { [weak self] in
            Task { @MainActor in
                self?.onStreamClosed?()
            }
        }
        self.eventReceiver = receiver
        try conn.addConnectionEventsReceiver(cb: receiver)

        connectionState = .connected
    }

    func disconnect() {
        readTask?.cancel()
        readTask = nil
        reconnectTask?.cancel()
        reconnectTask = nil

        if let stream = stream {
            stream.abort()
            self.stream = nil
        }

        if let conn = connection {
            if let receiver = eventReceiver {
                conn.removeConnectionEventsReceiver(cb: receiver)
            }
            conn.stop()
            self.connection = nil
        }

        if let c = client {
            c.stop()
            self.client = nil
        }

        eventReceiver = nil
        connectionState = .disconnected
        currentSession = nil
    }

    // MARK: - Pairing
    // TODO: Replace direct CoAP pairing with NabtoEdgeIamUtil once its xcframework
    // deployment target is fixed (currently conflicts with CBORCoding on iOS 16+).

    func pair(info: PairingInfo) async throws -> DeviceBookmark {
        disconnect()

        let c = Client()
        self.client = c

        let privateKey = try loadOrCreatePrivateKey()

        let conn = try c.createConnection()
        self.connection = conn

        try conn.setPrivateKey(key: privateKey)
        try conn.setProductId(id: info.productId)
        try conn.setDeviceId(id: info.deviceId)
        try conn.setServerConnectToken(sct: info.sct)

        connectionState = .connecting
        try await conn.connectAsync()

        try await conn.passwordAuthenticateAsync(username: info.username, password: info.password)

        // Pair via direct CoAP: try password-invite first, fall back to password-open (demo mode)
        let paired = try await coapPairPasswordInvite(conn: conn, username: info.username)
        if !paired {
            let openPaired = try await coapPairPasswordOpen(conn: conn, username: info.username)
            if !openPaired {
                throw NabtoError.pairingFailed("The invitation may have already been used.")
            }
        }

        let fingerprint = try conn.getDeviceFingerprintHex()

        let bookmark = DeviceBookmark(
            productId: info.productId,
            deviceId: info.deviceId,
            fingerprint: fingerprint,
            sct: info.sct,
            name: info.deviceId,
            lastSession: nil,
            lastConnected: Date()
        )

        // Close the pairing connection
        try? conn.close()
        self.connection = nil
        self.client = nil
        connectionState = .disconnected

        return bookmark
    }

    // MARK: - CoAP: Pairing (temporary, replaces IamUtil)

    /// POST /iam/pairing/password-invite with CBOR {"Username": "<username>"}
    private func coapPairPasswordInvite(conn: Connection, username: String) async throws -> Bool {
        let coap = try conn.createCoapRequest(method: "POST", path: "/iam/pairing/password-invite")
        let cbor: CBOR = .map([.utf8String("Username"): .utf8String(username)])
        let payload = Data(cbor.encode())
        try coap.setRequestPayload(contentFormat: ContentFormat.APPLICATION_CBOR.rawValue, data: payload)
        let response = try await coap.executeAsync()
        return response.status >= 200 && response.status < 300
    }

    /// POST /iam/pairing/password-open with CBOR {"Username": "<username>"}
    private func coapPairPasswordOpen(conn: Connection, username: String) async throws -> Bool {
        let coap = try conn.createCoapRequest(method: "POST", path: "/iam/pairing/password-open")
        let cbor: CBOR = .map([.utf8String("Username"): .utf8String(username)])
        let payload = Data(cbor.encode())
        try coap.setRequestPayload(contentFormat: ContentFormat.APPLICATION_CBOR.rawValue, data: payload)
        let response = try await coap.executeAsync()
        return response.status >= 200 && response.status < 300
    }

    // MARK: - CoAP: Sessions

    func listSessions() async throws -> [SessionInfo] {
        guard let conn = connection else {
            throw NabtoError.connectionFailed("Not connected")
        }

        let coap = try conn.createCoapRequest(method: "GET", path: "/terminal/sessions")
        let response = try await coap.executeAsync()

        guard response.status == 205 else {
            throw NabtoError.coapFailed("List sessions", response.status)
        }

        guard let payload = response.payload else { return [] }
        return CBORHelpers.decodeSessions(from: payload)
    }

    func attach(session: String, cols: Int, rows: Int) async throws {
        guard let conn = connection else {
            throw NabtoError.connectionFailed("Not connected")
        }

        let coap = try conn.createCoapRequest(method: "POST", path: "/terminal/attach")
        let payload = CBORHelpers.encodeAttach(session: session, cols: cols, rows: rows)
        try coap.setRequestPayload(contentFormat: ContentFormat.APPLICATION_CBOR.rawValue, data: payload)
        let response = try await coap.executeAsync()

        guard response.status == 201 else {
            if response.status == 404 {
                throw NabtoError.sessionNotFound(session)
            }
            throw NabtoError.coapFailed("Attach", response.status)
        }

        currentSession = session
    }

    func createSession(name: String, cols: Int, rows: Int, command: String? = nil) async throws {
        guard let conn = connection else {
            throw NabtoError.connectionFailed("Not connected")
        }

        let coap = try conn.createCoapRequest(method: "POST", path: "/terminal/create")
        let payload = CBORHelpers.encodeCreate(session: name, cols: cols, rows: rows, command: command)
        try coap.setRequestPayload(contentFormat: ContentFormat.APPLICATION_CBOR.rawValue, data: payload)
        let response = try await coap.executeAsync()

        guard response.status == 201 else {
            throw NabtoError.coapFailed("Create session", response.status)
        }
    }

    func resize(cols: Int, rows: Int) async {
        guard let conn = connection else { return }

        do {
            let coap = try conn.createCoapRequest(method: "POST", path: "/terminal/resize")
            let payload = CBORHelpers.encodeResize(cols: cols, rows: rows)
            try coap.setRequestPayload(contentFormat: ContentFormat.APPLICATION_CBOR.rawValue, data: payload)
            let response = try await coap.executeAsync()
            // Silent retry on failure (resize is not critical)
            if response.status != 204 {
                let coap2 = try conn.createCoapRequest(method: "POST", path: "/terminal/resize")
                try coap2.setRequestPayload(contentFormat: ContentFormat.APPLICATION_CBOR.rawValue, data: payload)
                _ = try await coap2.executeAsync()
            }
        } catch {
            // Resize failures are non-critical
        }
    }

    // MARK: - Stream

    func openStream() async throws {
        guard let conn = connection else {
            throw NabtoError.connectionFailed("Not connected")
        }

        let s = try conn.createStream()
        try await s.openAsync(streamPort: 1)
        self.stream = s

        startReadLoop()
    }

    func writeToStream(_ data: Data) {
        guard let stream = stream else { return }
        Task {
            do {
                try await stream.writeAsync(data: data)
            } catch {
                // Write failure handled by read loop ending
            }
        }
    }

    func closeStream() {
        readTask?.cancel()
        readTask = nil
        if let stream = stream {
            stream.abort()
            self.stream = nil
        }
        currentSession = nil
    }

    private func startReadLoop() {
        readTask?.cancel()
        readTask = Task { [weak self] in
            guard let self = self, let stream = self.stream else { return }
            while !Task.isCancelled {
                do {
                    let data = try await stream.readSomeAsync()
                    let bytes = [UInt8](data)
                    await MainActor.run {
                        self.onStreamData?(bytes)
                    }
                } catch {
                    if !Task.isCancelled {
                        await MainActor.run {
                            self.onStreamClosed?()
                        }
                    }
                    break
                }
            }
        }
    }

    // MARK: - Reconnect

    func attemptReconnect(bookmark: DeviceBookmark, session: String, cols: Int, rows: Int) {
        reconnectTask?.cancel()
        reconnectTask = Task { [weak self] in
            guard let self = self else { return }

            let startTime = Date()
            var attempt = 0

            while !Task.isCancelled {
                attempt += 1
                await MainActor.run {
                    self.connectionState = .reconnecting(attempt: attempt)
                }

                let elapsed = Date().timeIntervalSince(startTime)
                if self.reconnectLogic.shouldGiveUp(elapsedTime: elapsed) {
                    await MainActor.run {
                        self.connectionState = .offline
                    }
                    return
                }

                do {
                    try await self.connect(bookmark: bookmark)
                    try await self.attach(session: session, cols: cols, rows: rows)
                    try await self.openStream()
                    await self.resize(cols: cols, rows: rows)
                    await MainActor.run {
                        self.connectionState = .connected
                    }
                    return
                } catch {
                    let delay = self.reconnectLogic.backoff(attempt: attempt)
                    try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
                }
            }
        }
    }

    func cancelReconnect() {
        reconnectTask?.cancel()
        reconnectTask = nil
    }
}
