import Foundation

@Observable
class AppState {
    let bookmarkStore: BookmarkStore
    let nabtoService: NabtoService

    init() {
        let store = BookmarkStore()
        self.bookmarkStore = store
        self.nabtoService = NabtoService(bookmarkStore: store)
        injectTestConfigIfNeeded()
    }

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
        }

        guard let config = try? JSONDecoder().decode(TestConfig.self, from: data) else { return }

        let bookmark = DeviceBookmark(
            productId: config.productId,
            deviceId: config.deviceId,
            fingerprint: config.fingerprint,
            sct: config.sct,
            name: config.deviceId,
            lastSession: nil,
            lastConnected: nil
        )
        bookmarkStore.addDevice(bookmark)
    }
}
