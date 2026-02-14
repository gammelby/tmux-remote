import XCTest
@testable import TmuxRemote

final class BookmarkStoreTests: XCTestCase {

    private func makeDefaults() -> UserDefaults {
        let suiteName = "test.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suiteName)!
        defaults.removePersistentDomain(forName: suiteName)
        return defaults
    }

    private func makeBookmark(
        deviceId: String = "de-test",
        productId: String = "pr-test",
        session: String? = nil
    ) -> DeviceBookmark {
        DeviceBookmark(
            productId: productId,
            deviceId: deviceId,
            fingerprint: "abc123",
            sct: "tok",
            name: deviceId,
            lastSession: session,
            lastConnected: nil
        )
    }

    func testInitEmpty() {
        let store = BookmarkStore(defaults: makeDefaults())
        XCTAssertTrue(store.devices.isEmpty)
        XCTAssertNil(store.lastDeviceId)
    }

    func testAddDevice() {
        let store = BookmarkStore(defaults: makeDefaults())
        store.addDevice(makeBookmark())
        XCTAssertEqual(store.devices.count, 1)
        XCTAssertNotNil(store.bookmark(for: "de-test"))
    }

    func testAddDuplicateDevice() {
        let store = BookmarkStore(defaults: makeDefaults())
        store.addDevice(makeBookmark(deviceId: "de-dup"))
        store.addDevice(DeviceBookmark(
            productId: "pr-updated",
            deviceId: "de-dup",
            fingerprint: "new-fp",
            sct: "new-tok",
            name: "de-dup",
            lastSession: nil,
            lastConnected: nil
        ))
        XCTAssertEqual(store.devices.count, 1)
        XCTAssertEqual(store.bookmark(for: "de-dup")?.productId, "pr-updated")
        XCTAssertEqual(store.bookmark(for: "de-dup")?.fingerprint, "new-fp")
    }

    func testRemoveDevice() {
        let defaults = makeDefaults()
        let store = BookmarkStore(defaults: defaults)
        store.addDevice(makeBookmark(deviceId: "de-remove"))
        store.updateLastSession(deviceId: "de-remove", session: "main")
        XCTAssertEqual(store.lastDeviceId, "de-remove")

        store.removeDevice(id: "de-remove")
        XCTAssertTrue(store.devices.isEmpty)
        XCTAssertNil(store.lastDeviceId)
    }

    func testRemoveNonexistent() {
        let store = BookmarkStore(defaults: makeDefaults())
        store.addDevice(makeBookmark(deviceId: "de-keep"))
        store.removeDevice(id: "de-doesnotexist")
        XCTAssertEqual(store.devices.count, 1)
    }

    func testUpdateLastSession() {
        let store = BookmarkStore(defaults: makeDefaults())
        store.addDevice(makeBookmark(deviceId: "de-sess"))
        store.updateLastSession(deviceId: "de-sess", session: "work")
        XCTAssertEqual(store.bookmark(for: "de-sess")?.lastSession, "work")
        XCTAssertNotNil(store.bookmark(for: "de-sess")?.lastConnected)
        XCTAssertEqual(store.lastDeviceId, "de-sess")
    }

    func testLookupExisting() {
        let store = BookmarkStore(defaults: makeDefaults())
        store.addDevice(makeBookmark(deviceId: "de-find"))
        let result = store.bookmark(for: "de-find")
        XCTAssertNotNil(result)
        XCTAssertEqual(result?.deviceId, "de-find")
    }

    func testLookupNonexistent() {
        let store = BookmarkStore(defaults: makeDefaults())
        XCTAssertNil(store.bookmark(for: "de-nope"))
    }

    func testPersistenceRoundTrip() {
        let defaults = makeDefaults()
        let store1 = BookmarkStore(defaults: defaults)
        store1.addDevice(makeBookmark(deviceId: "de-persist"))
        store1.updateLastSession(deviceId: "de-persist", session: "round")

        let store2 = BookmarkStore(defaults: defaults)
        XCTAssertEqual(store2.devices.count, 1)
        XCTAssertEqual(store2.bookmark(for: "de-persist")?.lastSession, "round")
        XCTAssertEqual(store2.lastDeviceId, "de-persist")
    }
}
