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

    var isSessionNotFound: Bool {
        if case .sessionNotFound = self { return true }
        return false
    }

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

@MainActor
@Observable
class NabtoService {
    var currentDeviceId: String?
    var currentSession: String?

    private let connectionManager: ConnectionManager
    private let bookmarkStore: BookmarkStore

    private var stream: NabtoEdgeClient.Stream?
    private var readTask: Task<Void, Never>?
    private(set) var reconnectTask: Task<Void, Never>?
    private var reconnectContext: (deviceId: String, session: String)?

    /// Called on main actor when stream data arrives. Set by TerminalScreen.
    var onStreamData: (([UInt8]) -> Void)?

    /// Called on main actor when the stream ends unexpectedly.
    var onStreamClosed: (() -> Void)?

    private let reconnectLogic = ReconnectLogic()

    init(connectionManager: ConnectionManager, bookmarkStore: BookmarkStore) {
        self.connectionManager = connectionManager
        self.bookmarkStore = bookmarkStore
    }

    /// Connection state for the current device, derived from ConnectionManager.
    var connectionState: ConnectionState {
        guard let deviceId = currentDeviceId else { return .disconnected }
        return connectionManager.deviceStates[deviceId] ?? .disconnected
    }

    // MARK: - Connection

    func connect(bookmark: DeviceBookmark) async throws {
        currentDeviceId = bookmark.deviceId
        _ = try await connectionManager.connection(for: bookmark)
    }

    /// Disconnect terminal resources for the current device.
    /// When keepConnection is true, keep the underlying Nabto connection cached.
    func disconnect(keepConnection: Bool = false) {
        disableReconnectContext()
        onStreamClosed = nil
        onStreamData = nil

        readTask?.cancel()
        readTask = nil

        if let stream = stream {
            if keepConnection {
                do {
                    try stream.close()
                } catch {
                    // If graceful close fails, force-close to avoid leaking stream resources.
                    stream.abort()
                }
            } else {
                stream.abort()
            }
        }
        stream = nil

        if let deviceId = currentDeviceId {
            connectionManager.cancelPendingConnect(deviceId: deviceId)
            if !keepConnection {
                connectionManager.disconnect(deviceId: deviceId)
            }
        }

        currentSession = nil
        currentDeviceId = nil
    }

    func enableReconnectContext(deviceId: String, session: String) {
        reconnectContext = (deviceId, session)
    }

    func disableReconnectContext() {
        reconnectContext = nil
        cancelReconnect()
    }

    func canAutoReconnect(deviceId: String, session: String) -> Bool {
        guard let ctx = reconnectContext else { return false }
        return ctx.deviceId == deviceId && ctx.session == session
    }

    // MARK: - Pairing

    func pair(info: PairingInfo) async throws -> DeviceBookmark {
        let conn = try await connectionManager.connectForPairing(info: info)

        do {
            try await conn.passwordAuthenticateAsync(username: info.username, password: info.password)

            let paired = try await coapPairPasswordInvite(conn: conn, username: info.username)
            if !paired {
                throw NabtoError.pairingFailed("The invitation may have already been used.")
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

            connectionManager.adoptConnection(conn, for: info.deviceId)
            return bookmark
        } catch {
            conn.stop()
            connectionManager.setDeviceState(.disconnected, for: info.deviceId)
            throw error
        }
    }

    // MARK: - CoAP: Pairing (temporary, replaces IamUtil)

    private func coapPairPasswordInvite(conn: Connection, username: String) async throws -> Bool {
        let coap = try conn.createCoapRequest(method: "POST", path: "/iam/pairing/password-invite")
        let cbor: CBOR = .map([.utf8String("Username"): .utf8String(username)])
        let payload = Data(cbor.encode())
        try coap.setRequestPayload(contentFormat: ContentFormat.APPLICATION_CBOR.rawValue, data: payload)
        let response = try await coap.executeAsync()
        return response.status >= 200 && response.status < 300
    }

    // MARK: - CoAP: Sessions

    func listSessions(bookmark: DeviceBookmark) async throws -> [SessionInfo] {
        let conn = try await connectionManager.connection(for: bookmark)

        let coap = try conn.createCoapRequest(method: "GET", path: "/terminal/sessions")
        let response = try await coap.executeAsync()

        guard response.status == 205 else {
            throw NabtoError.coapFailed("List sessions", response.status)
        }

        guard let payload = response.payload else { return [] }
        return CBORHelpers.decodeSessions(from: payload)
    }

    func attach(bookmark: DeviceBookmark, session: String, cols: Int, rows: Int) async throws {
        let conn = try await connectionManager.connection(for: bookmark)

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

        currentDeviceId = bookmark.deviceId
        currentSession = session
    }

    func createSession(bookmark: DeviceBookmark, name: String, cols: Int, rows: Int, command: String? = nil) async throws {
        let conn = try await connectionManager.connection(for: bookmark)

        let coap = try conn.createCoapRequest(method: "POST", path: "/terminal/create")
        let payload = CBORHelpers.encodeCreate(session: name, cols: cols, rows: rows, command: command)
        try coap.setRequestPayload(contentFormat: ContentFormat.APPLICATION_CBOR.rawValue, data: payload)
        let response = try await coap.executeAsync()

        guard response.status == 201 else {
            throw NabtoError.coapFailed("Create session", response.status)
        }
    }

    func resize(bookmark: DeviceBookmark, cols: Int, rows: Int) async {
        do {
            let conn = try await connectionManager.connection(for: bookmark)
            let coap = try conn.createCoapRequest(method: "POST", path: "/terminal/resize")
            let payload = CBORHelpers.encodeResize(cols: cols, rows: rows)
            try coap.setRequestPayload(contentFormat: ContentFormat.APPLICATION_CBOR.rawValue, data: payload)
            let response = try await coap.executeAsync()
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

    func openStream(bookmark: DeviceBookmark) async throws {
        let conn = try await connectionManager.connection(for: bookmark)

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
                    self.onStreamData?(bytes)
                } catch {
                    if !Task.isCancelled {
                        self.onStreamClosed?()
                    }
                    break
                }
            }
        }
    }

    // MARK: - Reconnect

    func attemptReconnect(bookmark: DeviceBookmark, session: String, cols: Int, rows: Int,
                           onSuccess: (() -> Void)? = nil, onGiveUp: (() -> Void)? = nil) {
        guard canAutoReconnect(deviceId: bookmark.deviceId, session: session) else {
            return
        }

        reconnectTask?.cancel()
        reconnectTask = Task { [weak self] in
            guard let self = self else { return }

            let startTime = Date()
            var attempt = 0

            while !Task.isCancelled {
                guard self.canAutoReconnect(deviceId: bookmark.deviceId, session: session) else {
                    return
                }
                attempt += 1

                let elapsed = Date().timeIntervalSince(startTime)
                if self.reconnectLogic.shouldGiveUp(elapsedTime: elapsed) {
                    guard self.canAutoReconnect(deviceId: bookmark.deviceId, session: session) else {
                        return
                    }
                    self.connectionManager.setDeviceState(.offline, for: bookmark.deviceId)
                    onGiveUp?()
                    return
                }

                do {
                    self.connectionManager.disconnect(deviceId: bookmark.deviceId)
                    self.connectionManager.setDeviceState(.reconnecting(attempt: attempt), for: bookmark.deviceId)
                    try await self.connect(bookmark: bookmark)
                    try await self.attach(bookmark: bookmark, session: session, cols: cols, rows: rows)
                    try await self.openStream(bookmark: bookmark)
                    await self.resize(bookmark: bookmark, cols: cols, rows: rows)
                    guard self.canAutoReconnect(deviceId: bookmark.deviceId, session: session) else {
                        self.connectionManager.disconnect(deviceId: bookmark.deviceId)
                        return
                    }
                    onSuccess?()
                    return
                } catch is CancellationError {
                    return
                } catch let error as NabtoError where error.isSessionNotFound {
                    // Session was destroyed; retrying will never succeed.
                    self.connectionManager.setDeviceState(.offline, for: bookmark.deviceId)
                    onGiveUp?()
                    return
                } catch {
                    if Task.isCancelled || !self.canAutoReconnect(deviceId: bookmark.deviceId, session: session) {
                        return
                    }
                    self.connectionManager.setDeviceState(.reconnecting(attempt: attempt), for: bookmark.deviceId)
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
