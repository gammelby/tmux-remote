import XCTest

/// UI integration tests for tmux-remote.
///
/// These tests require a running tmux-remote-agent with a pre-paired user.
/// Connection details are read from `tests/test_config.json` (same fixture
/// as the CLI integration tests). The SCT is extracted automatically from
/// `<agent_home_dir>/state/iam_state.json`.
///
/// To run: start the agent, ensure `tests/test_config.json` exists, then
/// execute this test suite.
final class TmuxRemoteUITests: XCTestCase {

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

    /// Expands the device row to reveal inline sessions.
    /// Waits for probe to complete first so the device is reachable.
    private func expandDevice() {
        let status = waitForProbeComplete()
        XCTAssertNotEqual(status.label, "Offline", "Device should be online")

        let deviceRow = app.buttons["device-row-\(deviceId!)"]
        XCTAssertTrue(deviceRow.exists, "Device row should exist")
        deviceRow.tap()
    }

    /// Waits for at least one session row to appear under the expanded device.
    /// Returns the count of session rows found.
    @discardableResult
    private func waitForSessionRows(timeout: TimeInterval = 15) -> Int {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            let sessionRows = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'session-row-'"))
            if sessionRows.count > 0 {
                return sessionRows.count
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.5))
        }
        return 0
    }

    /// Expands the device and taps the first session row to navigate to terminal.
    private func navigateToTerminal() {
        expandDevice()

        let count = waitForSessionRows()
        XCTAssertTrue(count > 0, "Should find at least one session row")

        let sessionRows = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'session-row-'"))
        sessionRows.element(boundBy: 0).tap()

        let pill = app.staticTexts["connection-pill"]
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

    func testExpandDeviceSessions() throws {
        app.launch()
        expandDevice()

        // Sessions should appear inline (no screen push)
        let sessionRows = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'session-row-'"))
        let emptyLabel = app.staticTexts.matching(NSPredicate(format: "identifier BEGINSWITH 'sessions-empty-'"))
        let newSessionButton = app.buttons["new-session-\(deviceId!)"]

        // Wait for content to appear
        let deadline = Date().addingTimeInterval(15)
        while Date() < deadline {
            if sessionRows.count > 0 || emptyLabel.count > 0 {
                break
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.5))
        }

        let hasContent = sessionRows.count > 0 || emptyLabel.count > 0
        XCTAssertTrue(hasContent, "Should show session rows or empty state inline")

        // "New Session" button should be visible
        XCTAssertTrue(newSessionButton.exists, "New Session button should appear under expanded device")

        // We should still be on the Devices screen (not pushed to a new view)
        let navTitle = app.navigationBars["Devices"]
        XCTAssertTrue(navTitle.exists, "Should still be on Devices screen")
    }

    func testDeviceRowExpandCollapse() throws {
        app.launch()
        let status = waitForProbeComplete()
        XCTAssertNotEqual(status.label, "Offline", "Device should be online")

        let deviceRow = app.buttons["device-row-\(deviceId!)"]

        // Expand
        deviceRow.tap()
        let newSessionButton = app.buttons["new-session-\(deviceId!)"]
        let deadline = Date().addingTimeInterval(10)
        while Date() < deadline {
            if newSessionButton.exists { break }
            RunLoop.current.run(until: Date().addingTimeInterval(0.5))
        }
        XCTAssertTrue(newSessionButton.exists, "New Session button should appear when expanded")

        // Collapse
        deviceRow.tap()
        // Give animation time to complete
        RunLoop.current.run(until: Date().addingTimeInterval(1))
        XCTAssertFalse(newSessionButton.exists, "New Session button should disappear when collapsed")
    }

    func testInlineSessionRowNavigatesToTerminal() throws {
        app.launch()
        expandDevice()

        let count = waitForSessionRows()
        XCTAssertTrue(count > 0, "Should find at least one session row")

        let sessionRows = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'session-row-'"))
        sessionRows.element(boundBy: 0).tap()

        let pill = app.staticTexts["connection-pill"]
        XCTAssertTrue(pill.waitForExistence(timeout: 15), "Terminal screen should appear")
        XCTAssertEqual(pill.label, "Connected", "Connection pill should show Connected")
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

        // Expand device if not already expanded (control stream may keep it populated)
        let existingRows = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'session-row-'"))
        if existingRows.count == 0 {
            deviceRow.tap()
        }

        let count = waitForSessionRows()
        XCTAssertTrue(count > 0, "Should find session rows after re-expanding")

        let sessionRows = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'session-row-'"))
        sessionRows.element(boundBy: 0).tap()

        let pill = app.staticTexts["connection-pill"]
        XCTAssertTrue(pill.waitForExistence(timeout: 15), "Should reach terminal on re-entry")
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
        let deviceRow = app.buttons["device-row-\(deviceId!)"]

        // Either terminal resumes or device list appears (if resume fails gracefully)
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

    /// Tests the exact scenario: resume path -> back to devices -> device shows Online.
    ///
    /// This reproduces the bug where the app launches directly into TerminalScreen
    /// (because lastSession is saved), the user presses "Back to Devices", and the
    /// device status hangs at "Checking..." instead of resolving to "Online".
    func testResumeBackShowsDeviceOnline() throws {
        // Step 1: Launch normally and navigate to terminal to set lastSession
        app.launch()
        navigateToTerminal()

        let pill = app.staticTexts["connection-pill"]
        XCTAssertTrue(pill.exists, "Should be on terminal screen")

        // Step 2: Terminate and relaunch with --preserve-session so the app
        // resumes directly into TerminalScreen (preserving the saved lastSession)
        app.terminate()
        sleep(1)
        app.launchArguments.append("--preserve-session")
        app.launch()

        // The app should go straight to terminal (resume path)
        let resumedPill = app.staticTexts["connection-pill"]
        XCTAssertTrue(resumedPill.waitForExistence(timeout: 15),
                       "App should resume directly into terminal screen")

        // Wait for the connection to be established
        let deadline = Date().addingTimeInterval(15)
        while Date() < deadline {
            if resumedPill.label == "Connected" { break }
            RunLoop.current.run(until: Date().addingTimeInterval(0.5))
        }
        XCTAssertEqual(resumedPill.label, "Connected",
                        "Terminal should show Connected before navigating back")

        // Step 3: Tap "Back to Devices"
        let backButton = app.buttons["back-to-devices"]
        XCTAssertTrue(backButton.exists, "Back to Devices button should exist")
        backButton.tap()

        // Step 4: Device list should appear
        let deviceRow = app.buttons["device-row-\(deviceId!)"]
        XCTAssertTrue(deviceRow.waitForExistence(timeout: 15),
                       "Device list should appear after tapping back")

        // Step 5: Probe should complete and show Online (not hang at Checking...)
        let status = waitForProbeComplete(timeout: 30)
        XCTAssertNotEqual(status.label, "Offline",
                          "Device should be Online after resume-path back navigation")
        XCTAssertNotEqual(status.label, "Checking...",
                          "Probe should not hang at Checking...")
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

    func testNewSessionFromInlineButton() throws {
        throw XCTSkip("Requires live agent for session creation")
    }

    func testSessionGoneFallback() throws {
        throw XCTSkip("Requires external tmux session management")
    }

    func testDeviceUnreachableFallback() throws {
        throw XCTSkip("Requires external agent lifecycle management")
    }
}
