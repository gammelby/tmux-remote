import Foundation

@MainActor
@Observable
class AppState {
    let bookmarkStore: BookmarkStore
    let connectionManager: ConnectionManager
    let nabtoService: NabtoService

    init() {
        let store = BookmarkStore()
        let cm = ConnectionManager()
        self.bookmarkStore = store
        self.connectionManager = cm

        #if DEBUG
        let args = ProcessInfo.processInfo.arguments
        if args.contains("--stub-terminal") {
            let stub = StubNabtoService(connectionManager: cm, bookmarkStore: store)
            Self.configureStub(stub, args: args)
            self.nabtoService = stub
        } else {
            self.nabtoService = NabtoService(connectionManager: cm, bookmarkStore: store)
        }
        #else
        self.nabtoService = NabtoService(connectionManager: cm, bookmarkStore: store)
        #endif

        injectTestConfigIfNeeded()
    }

    #if DEBUG
    private static func configureStub(_ stub: StubNabtoService, args: [String]) {
        guard let idx = args.firstIndex(of: "--stub-script-b64"),
              idx + 1 < args.count else {
            AppLog.log("configureStub: --stub-script-b64 not found in %d args", args.count)
            return
        }
        let b64 = args[idx + 1]
        guard let data = Data(base64Encoded: b64) else {
            AppLog.log("configureStub: base64 decode failed, length=%d", b64.count)
            return
        }
        do {
            let script = try JSONDecoder().decode(StubScript.self, from: data)
            stub.scriptedEvents = script.events
            AppLog.log("configureStub: loaded %d events", stub.scriptedEvents.count)
        } catch {
            AppLog.log("configureStub: decode failed: %@", String(describing: error))
        }
    }
    #endif

    private func injectTestConfigIfNeeded() {
        let args = ProcessInfo.processInfo.arguments
        guard let idx = args.firstIndex(of: "--test-config"),
              idx + 1 < args.count else { return }
        let json = args[idx + 1]
        guard let data = json.data(using: .utf8) else { return }

        struct TestConfig: Codable {
            var productId: String
            var deviceId: String
            var sct: String
            var fingerprint: String
            var privateKey: String?
        }

        guard let config = try? JSONDecoder().decode(TestConfig.self, from: data) else { return }

        // Always inject the test private key (overrides any existing key)
        if let key = config.privateKey, !key.isEmpty {
            _ = KeychainService.savePrivateKey(key)
        }

        // Preserve lastSession only when --preserve-session is passed (resume-path tests)
        let preserveSession = args.contains("--preserve-session")
        var preservedSession: String? = nil
        var preservedLastConnected: Date? = nil
        var wasLastDevice = false

        if preserveSession {
            let existingBookmark = bookmarkStore.bookmark(for: config.deviceId)
            preservedSession = existingBookmark?.lastSession
            preservedLastConnected = existingBookmark?.lastConnected
            wasLastDevice = bookmarkStore.lastDeviceId == config.deviceId
        }

        // In stub mode, set lastSession so the app goes straight to TerminalScreen
        let stubMode = args.contains("--stub-terminal")

        // Clear previous state so tests start from a clean device list
        for device in bookmarkStore.devices {
            bookmarkStore.removeDevice(id: device.deviceId)
        }

        let bookmark = DeviceBookmark(
            productId: config.productId,
            deviceId: config.deviceId,
            fingerprint: config.fingerprint,
            sct: config.sct,
            name: config.deviceId,
            lastSession: stubMode ? "stub-session" : preservedSession,
            lastConnected: stubMode ? Date() : preservedLastConnected
        )
        bookmarkStore.addDevice(bookmark)

        // Set lastDeviceId so resume path triggers
        if stubMode {
            bookmarkStore.lastDeviceId = config.deviceId
        } else if wasLastDevice && preservedSession != nil {
            bookmarkStore.lastDeviceId = config.deviceId
        }
    }
}
