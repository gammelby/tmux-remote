import XCTest

/// UI integration tests for NabtoShell.
///
/// These tests require a running nabtoshell-agent with a pre-paired user.
/// Connection details are read from `tests/test_config.json` (same fixture
/// as the CLI integration tests). The SCT is extracted automatically from
/// `<agent_home_dir>/state/iam_state.json`.
///
/// To run: start the agent, ensure `tests/test_config.json` exists, then
/// execute this test suite.
final class NabtoShellUITests: XCTestCase {

    private var app: XCUIApplication!
    private var deviceId: String!

    /// Finds the repo root by walking up from the source file location at compile time.
    private func repoRoot() throws -> URL {
        // #filePath resolves at compile time to the source file inside the repo
        var dir = URL(fileURLWithPath: #filePath)
        for _ in 0..<10 {
            dir = dir.deletingLastPathComponent()
            let candidate = dir.appendingPathComponent("tests/test_config.json")
            if FileManager.default.fileExists(atPath: candidate.path) {
                return dir
            }
        }
        throw XCTSkip("tests/test_config.json not found; skipping live agent tests")
    }

    /// Reads test_config.json and builds a JSON config string for the app.
    /// Uses the dedicated `ios_test_private_key` and `ios_test_sct` fields,
    /// which correspond to a pre-paired "uitest" user on the agent.
    private func loadTestConfig() throws -> String {
        let root = try repoRoot()
        let configURL = root.appendingPathComponent("tests/test_config.json")
        let data = try Data(contentsOf: configURL)
        let json = try JSONSerialization.jsonObject(with: data) as! [String: Any]

        guard let productId = json["product_id"] as? String,
              let devId = json["device_id"] as? String else {
            throw XCTSkip("test_config.json missing product_id or device_id")
        }

        guard let privateKey = json["ios_test_private_key"] as? String, !privateKey.isEmpty else {
            throw XCTSkip("test_config.json missing ios_test_private_key")
        }

        guard let sct = json["ios_test_sct"] as? String, !sct.isEmpty else {
            throw XCTSkip("test_config.json missing ios_test_sct")
        }

        let config: [String: String] = [
            "productId": productId,
            "deviceId": devId,
            "sct": sct,
            "fingerprint": (json["fingerprint"] as? String) ?? "",
            "privateKey": privateKey
        ]
        let configData = try JSONSerialization.data(withJSONObject: config)
        return String(data: configData, encoding: .utf8)!
    }

    override func setUpWithError() throws {
        continueAfterFailure = false
        app = XCUIApplication()
        app.launchEnvironment["UI_TESTING"] = "1"

        let configJSON = try loadTestConfig()
        app.launchArguments = ["--test-config", configJSON]

        // Parse deviceId for accessibility identifier lookups
        let data = configJSON.data(using: .utf8)!
        let config = try JSONSerialization.jsonObject(with: data) as! [String: String]
        deviceId = config["deviceId"]
    }

    override func tearDown() {
        app?.terminate()
        app = nil
        deviceId = nil
    }

    // MARK: - Helpers

    /// Waits for the device status label to show something other than "Checking...",
    /// indicating the probe completed (online or offline).
    /// Uses manual polling to avoid XCTest expectation stalling issues.
    @discardableResult
    private func waitForProbeComplete(timeout: TimeInterval = 15) -> XCUIElement {
        let status = app.staticTexts["device-status-\(deviceId!)"]
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if status.exists && status.label != "Checking..." {
                return status
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.5))
        }
        XCTFail("Probe did not complete within \(timeout)s (still showing '\(status.exists ? status.label : "not found")')")
        return status
    }

    /// Navigates from device list to the session list by tapping the device row.
    /// Waits for the probe to complete first so the device is reachable.
    private func navigateToSessionList() {
        let status = waitForProbeComplete()
        XCTAssertNotEqual(status.label, "Offline", "Device should be online")

        let deviceRow = app.buttons["device-row-\(deviceId!)"]
        XCTAssertTrue(deviceRow.exists, "Device row should exist")
        deviceRow.tap()
    }

    /// Waits for session list to finish loading (loading indicator disappears).
    /// Returns true if sessions were found, false if "No sessions" appeared.
    @discardableResult
    private func waitForSessionsLoaded(timeout: TimeInterval = 15) -> Bool {
        let loading = app.activityIndicators["sessions-loading"]
        if loading.exists {
            let gone = NSPredicate(format: "exists == false")
            expectation(for: gone, evaluatedWith: loading)
            waitForExpectations(timeout: timeout)
        }

        let empty = app.staticTexts["sessions-empty"]
        let error = app.staticTexts["sessions-error"]
        XCTAssertFalse(error.exists, "Session list should not show an error")

        return !empty.exists
    }

    /// Navigates from device list all the way to a terminal session.
    /// If the agent has exactly one tmux session, auto-attach skips the
    /// session list and goes straight to the terminal.
    private func navigateToTerminal() {
        navigateToSessionList()

        let pill = app.staticTexts["connection-pill"]

        // Auto-attach may have already pushed to the terminal
        if pill.waitForExistence(timeout: 3) {
            return
        }

        // Otherwise we are on the session list; tap the first session row
        let sessionRows = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'session-row-'"))
        XCTAssertTrue(sessionRows.count > 0, "Should find at least one session row")
        sessionRows.element(boundBy: 0).tap()

        XCTAssertTrue(pill.waitForExistence(timeout: 15), "Terminal screen should appear")
    }

    // MARK: - Tests

    func testDeviceListShowsOnline() throws {
        app.launch()

        let deviceRow = app.buttons["device-row-\(deviceId!)"]
        XCTAssertTrue(deviceRow.waitForExistence(timeout: 15), "Device row should appear")

        let status = waitForProbeComplete()
        XCTAssertNotEqual(status.label, "Offline", "Device should be reachable")
    }

    func testNavigateToSessionList() throws {
        app.launch()
        navigateToSessionList()

        // If there is exactly 1 tmux session, auto-attach navigates straight to
        // the terminal. Accept either session rows, empty state, or terminal view.
        // The connection-pill is unique to TerminalScreen.
        let pill = app.staticTexts["connection-pill"]
        let empty = app.staticTexts["sessions-empty"]
        let sessionRows = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'session-row-'"))

        let appeared = pill.waitForExistence(timeout: 15) || empty.exists || sessionRows.count > 0
        XCTAssertTrue(appeared, "Should show sessions, empty state, or auto-attach to terminal")
    }

    func testNavigateToTerminal() throws {
        app.launch()
        navigateToTerminal()

        let pill = app.staticTexts["connection-pill"]
        XCTAssertTrue(pill.waitForExistence(timeout: 5), "Connection pill should exist")
        XCTAssertEqual(pill.label, "Connected", "Connection pill should show Connected")
    }

    func testBackToDevicesFromTerminal() throws {
        app.launch()
        navigateToTerminal()

        // Tap "back-to-devices" button
        let backButton = app.buttons["back-to-devices"]
        XCTAssertTrue(backButton.exists, "Back to Devices button should exist")
        backButton.tap()

        // Device list should appear (verify by device row)
        let deviceRow = app.buttons["device-row-\(deviceId!)"]
        XCTAssertTrue(deviceRow.waitForExistence(timeout: 15), "Should return to device list")

        // The device probe should complete and NOT hang on "Checking..."
        // (this is the stale-connection regression test)
        let status = waitForProbeComplete(timeout: 30)
        XCTAssertNotEqual(status.label, "Offline",
                          "Device should be online after returning from terminal")
    }

    func testBackToDevicesAndReconnect() throws {
        app.launch()
        navigateToTerminal()

        // Go back to device list
        let backButton = app.buttons["back-to-devices"]
        backButton.tap()

        let deviceRow = app.buttons["device-row-\(deviceId!)"]
        XCTAssertTrue(deviceRow.waitForExistence(timeout: 10), "Should return to device list")

        // Wait for probe to complete
        let status = waitForProbeComplete(timeout: 15)
        XCTAssertNotEqual(status.label, "Offline")

        // Navigate to device again: should reach terminal or session list (not hang)
        deviceRow.tap()

        let pill = app.staticTexts["connection-pill"]
        let sessionRows = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'session-row-'"))
        let error = app.staticTexts["sessions-error"]

        // Wait for terminal (auto-attach) or session list
        let reached = pill.waitForExistence(timeout: 15) || sessionRows.count > 0
        XCTAssertTrue(reached, "Should reach terminal or session list on re-entry")
        XCTAssertFalse(error.exists, "Should not show error on re-entry")
    }

    func testResumeOnRelaunch() throws {
        app.launch()
        navigateToTerminal()

        // Terminate and relaunch
        app.terminate()
        sleep(1)
        app.launch()

        // On relaunch with lastSession set, the app should go straight to terminal
        let pill = app.staticTexts["connection-pill"]
        let deviceList = app.navigationBars["Devices"]

        // Either terminal resumes or device list appears (if resume fails gracefully)
        let deviceRow = app.buttons["device-row-\(deviceId!)"]
        let appeared = pill.waitForExistence(timeout: 10) || deviceRow.waitForExistence(timeout: 5)
        XCTAssertTrue(appeared, "App should show terminal or device list on relaunch")

        if pill.exists {
            // If we resumed, verify we can go back cleanly
            let backButton = app.buttons["back-to-devices"]
            if backButton.exists {
                backButton.tap()
                XCTAssertTrue(deviceRow.waitForExistence(timeout: 10))
                let status = waitForProbeComplete(timeout: 15)
                XCTAssertNotEqual(status.label, "Checking...",
                                  "Probe should complete after resume + back")
            }
        }
    }

    func testKeyboardAccessory() throws {
        app.launch()
        navigateToTerminal()

        // Tap the terminal area to bring up the keyboard
        let pill = app.staticTexts["connection-pill"]
        XCTAssertTrue(pill.exists, "Should be on terminal screen")
        // Tap below the pill to hit the terminal
        app.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)).tap()
        sleep(1)

        let escButton = app.buttons["Esc"]
        let tabButton = app.buttons["Tab"]
        let ctrlButton = app.buttons["Ctrl"]

        let hasAccessoryKeys = escButton.exists || tabButton.exists || ctrlButton.exists
        XCTAssertTrue(hasAccessoryKeys, "Keyboard accessory bar should have Esc/Tab/Ctrl buttons")
    }

    func testResizeOnRotation() throws {
        app.launch()
        navigateToTerminal()

        let pill = app.staticTexts["connection-pill"]
        XCTAssertTrue(pill.exists, "Should be on terminal screen before rotation")

        // Rotate to landscape
        XCUIDevice.shared.orientation = .landscapeLeft
        sleep(2)

        XCTAssertTrue(pill.exists, "Connection pill should survive landscape rotation")
        let backButton = app.buttons["back-to-devices"]
        XCTAssertTrue(backButton.exists, "Back button should survive rotation")

        // Rotate back
        XCUIDevice.shared.orientation = .portrait
        sleep(1)

        XCTAssertTrue(pill.exists, "Connection pill should survive portrait rotation")
    }

    func testSessionGoneFallback() throws {
        throw XCTSkip("Requires external tmux session management")
    }

    func testDeviceUnreachableFallback() throws {
        throw XCTSkip("Requires external agent lifecycle management")
    }
}
