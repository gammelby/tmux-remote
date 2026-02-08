import XCTest
@testable import NabtoShell

final class ResumeLogicTests: XCTestCase {

    private func makeBookmark(
        deviceId: String = "de-test",
        session: String? = nil
    ) -> DeviceBookmark {
        DeviceBookmark(
            productId: "pr-test",
            deviceId: deviceId,
            fingerprint: "abc",
            sct: "tok",
            name: deviceId,
            lastSession: session,
            lastConnected: nil
        )
    }

    func testResumeWithLastDeviceAndSession() {
        let bookmark = makeBookmark(deviceId: "de-1", session: "work")
        let result = resolveLaunchDestination(devices: [bookmark], lastDeviceId: "de-1")
        XCTAssertEqual(result, .resumeSession(bookmark: bookmark, session: "work"))
    }

    func testLastDeviceExistsButNoSession() {
        let bookmark = makeBookmark(deviceId: "de-1", session: nil)
        let result = resolveLaunchDestination(devices: [bookmark], lastDeviceId: "de-1")
        XCTAssertEqual(result, .deviceList)
    }

    func testLastDeviceIdSetButBookmarkMissing() {
        let bookmark = makeBookmark(deviceId: "de-other")
        let result = resolveLaunchDestination(devices: [bookmark], lastDeviceId: "de-gone")
        XCTAssertEqual(result, .deviceList)
    }

    func testNoDevicesAtAll() {
        let result = resolveLaunchDestination(devices: [], lastDeviceId: nil)
        XCTAssertEqual(result, .deviceList)
    }
}
