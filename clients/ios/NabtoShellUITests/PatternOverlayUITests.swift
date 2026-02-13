import XCTest

/// Stub-based UI tests for the pattern overlay.
///
/// These tests run entirely offline using `--stub-terminal` mode: no running
/// agent, no network, no external dependencies. A `StubNabtoService` replaces
/// the real Nabto SDK; scripted pattern events (matching what the agent control
/// stream pushes in production) drive the overlay.
///
/// The existing `NabtoShellUITests.swift` remains the live integration test
/// suite (requires a running agent + `test_config.json`).
final class PatternOverlayUITests: XCTestCase {

    private var app: XCUIApplication!

    // MARK: - Stub scripts

    /// Base64-encodes a stub script JSON for passing as a launch argument.
    /// Using base64 avoids JSON truncation in launch argument passing.
    private static func encodeScript(_ script: [String: Any]) -> String {
        let data = try! JSONSerialization.data(withJSONObject: script)
        return data.base64EncodedString()
    }

    /// Numbered menu script: delivers a pattern_match event with 3 menu items.
    private static let numberedMenuScript: String = encodeScript([
        "events": [
            [
                "type": "pattern_match",
                "delay": 0.1,
                "pattern_id": "numbered_prompt",
                "pattern_type": "numbered_menu",
                "prompt": "Do you want to proceed?",
                "actions": [
                    ["label": "Yes", "keys": "1"],
                    ["label": "Yes, and don't ask again", "keys": "2"],
                    ["label": "No", "keys": "3"]
                ]
            ]
        ]
    ])

    /// Yes/no script: delivers a pattern_match event with Allow/Deny actions.
    private static let yesNoScript: String = encodeScript([
        "events": [
            [
                "type": "pattern_match",
                "delay": 0.1,
                "pattern_id": "yes_no_prompt",
                "pattern_type": "yes_no",
                "prompt": NSNull(),
                "actions": [
                    ["label": "Allow", "keys": "y"],
                    ["label": "Deny", "keys": "n"]
                ]
            ]
        ]
    ])

    // MARK: - Test config

    /// Minimal test config to create a bookmark so the app can launch into TerminalScreen.
    private static let testConfig: String = {
        let config: [String: String] = [
            "productId": "pr-stub0000",
            "deviceId": "de-stub0000",
            "sct": "stubtoken",
            "fingerprint": "0000000000000000000000000000000000000000000000000000000000000000"
        ]
        let data = try! JSONSerialization.data(withJSONObject: config)
        return String(data: data, encoding: .utf8)!
    }()

    // MARK: - Setup

    override func setUpWithError() throws {
        continueAfterFailure = false
        app = XCUIApplication()
    }

    override func tearDown() {
        app?.terminate()
        app = nil
    }

    /// Launches the app in stub mode with the given base64-encoded script and optionally pre-selects an agent.
    private func launchStub(script: String, agent: String? = nil) {
        var args = [
            "--stub-terminal",
            "--stub-script-b64", script,
            "--test-config", Self.testConfig
        ]
        if let agent = agent {
            args += ["--stub-agent", agent]
        }
        app.launchArguments = args
        app.launch()
    }

    // MARK: - Helpers

    /// Waits for the connection pill (indicates TerminalScreen appeared).
    @discardableResult
    private func waitForTerminal(timeout: TimeInterval = 10) -> XCUIElement {
        let pill = app.staticTexts["connection-pill"]
        XCTAssertTrue(pill.waitForExistence(timeout: timeout), "Terminal screen should appear")
        return pill
    }

    /// Waits for the pattern overlay backdrop to appear (overlay is visible).
    @discardableResult
    private func waitForOverlay(timeout: TimeInterval = 10) -> XCUIElement {
        let backdrop = app.otherElements["pattern-overlay-backdrop"]
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if backdrop.exists { return backdrop }
            RunLoop.current.run(until: Date().addingTimeInterval(0.5))
        }
        XCTFail("Pattern overlay should appear within \(timeout)s")
        return backdrop
    }

    /// Waits for the pattern overlay backdrop to disappear.
    private func waitForOverlayDismissed(timeout: TimeInterval = 5) {
        let backdrop = app.otherElements["pattern-overlay-backdrop"]
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if !backdrop.exists { return }
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
        }
        XCTFail("Pattern overlay should have disappeared within \(timeout)s")
    }

    /// Finds a button by its label text.
    private func button(label: String) -> XCUIElement {
        app.buttons.matching(NSPredicate(format: "label == %@", label)).firstMatch
    }

    /// Taps a button via its center coordinate to avoid XCUITest idle-wait.
    /// In non-stub mode, SwiftTerm's cursor blink timer prevents the app from
    /// reaching quiescence, causing normal .tap() calls to wait ~120 seconds.
    private func tapButton(_ element: XCUIElement) {
        element.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)).tap()
    }

    // MARK: - Numbered Menu Tests

    func testNumberedMenuOverlayAppears() throws {
        launchStub(script: Self.numberedMenuScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        // The overlay should have a Dismiss button
        let dismiss = button(label: "Dismiss")
        XCTAssertTrue(dismiss.exists, "Dismiss button should be visible")
    }

    func testNumberedMenuItemCount() throws {
        launchStub(script: Self.numberedMenuScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        // Should have 3 menu items: Yes, Yes and don't ask again, No
        let yes = button(label: "Yes")
        let yesDontAsk = button(label: "Yes, and don't ask again")
        let no = button(label: "No")

        XCTAssertTrue(yes.exists, "Menu item 'Yes' should exist")
        XCTAssertTrue(yesDontAsk.exists, "Menu item 'Yes, and don't ask again' should exist")
        XCTAssertTrue(no.exists, "Menu item 'No' should exist")

        // Should not have a 4th item
        let allButtons = app.buttons.allElementsBoundByIndex
        let menuLabels = allButtons.filter { ["Yes", "Yes, and don't ask again", "No"].contains($0.label) }
        XCTAssertEqual(menuLabels.count, 3, "Should have exactly 3 menu items")
    }

    func testNumberedMenuPromptText() throws {
        launchStub(script: Self.numberedMenuScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        let promptTexts = app.staticTexts.matching(
            NSPredicate(format: "label CONTAINS 'Do you want to proceed'")
        )
        XCTAssertGreaterThan(promptTexts.count, 0,
                             "Prompt text containing 'Do you want to proceed' should exist")
    }

    func testTapMenuItemSendsKeys() throws {
        launchStub(script: Self.numberedMenuScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        // Tap first menu item "Yes" (should send "1")
        let yes = button(label: "Yes")
        XCTAssertTrue(yes.exists, "Menu item 'Yes' should exist")
        tapButton(yes)

        // Verify sent keys via debug label
        let sentKeys = app.staticTexts["debug-sent-keys"]
        let deadline = Date().addingTimeInterval(5)
        while Date() < deadline {
            if sentKeys.exists && !sentKeys.label.isEmpty { break }
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
        }
        XCTAssertTrue(sentKeys.exists, "Debug sent-keys label should exist")
        XCTAssertEqual(sentKeys.label, "1", "Tapping 'Yes' should send '1'")
    }

    func testTapMenuItemDismissesOverlay() throws {
        launchStub(script: Self.numberedMenuScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        let yes = button(label: "Yes")
        tapButton(yes)

        waitForOverlayDismissed()
    }

    func testDismissButtonClosesOverlay() throws {
        launchStub(script: Self.numberedMenuScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        let dismissButton = button(label: "Dismiss")
        XCTAssertTrue(dismissButton.exists, "Dismiss button should exist")
        tapButton(dismissButton)

        waitForOverlayDismissed()
    }

    // MARK: - Yes/No Tests

    func testYesNoOverlayAppears() throws {
        launchStub(script: Self.yesNoScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        // Should have binary actions: Allow and Deny
        let allow = button(label: "Allow")
        let deny = button(label: "Deny")
        XCTAssertTrue(allow.exists, "Allow action should exist")
        XCTAssertTrue(deny.exists, "Deny action should exist")
    }

    func testTapYesNoSendsKey() throws {
        launchStub(script: Self.yesNoScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        // Tap "Allow" (first action, sends "y")
        let allow = button(label: "Allow")
        tapButton(allow)

        let sentKeys = app.staticTexts["debug-sent-keys"]
        let deadline = Date().addingTimeInterval(5)
        while Date() < deadline {
            if sentKeys.exists && !sentKeys.label.isEmpty { break }
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
        }
        XCTAssertTrue(sentKeys.exists, "Debug sent-keys label should exist")
        XCTAssertEqual(sentKeys.label, "y", "Tapping Allow should send 'y'")
    }

    // MARK: - Keys Format Tests

    /// Verifies that tapping a numbered menu item sends just the number key,
    /// not number+newline.
    func testNumberedMenuSendsKeyWithoutNewline() throws {
        launchStub(script: Self.numberedMenuScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        let yes = button(label: "Yes")
        XCTAssertTrue(yes.exists, "Menu item 'Yes' should exist")
        tapButton(yes)

        let sentKeys = app.staticTexts["debug-sent-keys"]
        let deadline = Date().addingTimeInterval(5)
        while Date() < deadline {
            if sentKeys.exists && !sentKeys.label.isEmpty { break }
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
        }
        XCTAssertTrue(sentKeys.exists, "Debug sent-keys label should exist")
        XCTAssertEqual(sentKeys.label, "1",
                       "Numbered menu should send just the number, not number+newline")
    }

    // MARK: - Real Claude Code Data Tests

    /// Script that mimics a real Claude Code pattern match event with
    /// a long item 2 label (path-qualified command).
    private static let realClaudeCodeScript: String = encodeScript([
        "events": [
            [
                "type": "pattern_match",
                "delay": 0.1,
                "pattern_id": "numbered_prompt",
                "pattern_type": "numbered_menu",
                "prompt": "Do you want to proceed?",
                "actions": [
                    ["label": "Yes", "keys": "1"],
                    ["label": "Yes, and don't ask again for /Users/ug/git/qr-test/.venv/bin/python commands in /Users/ug/git/qr-test", "keys": "2"],
                    ["label": "No", "keys": "3"]
                ]
            ]
        ]
    ])

    func testRealClaudeCodePrompt() throws {
        launchStub(script: Self.realClaudeCodeScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()
        let dismiss = button(label: "Dismiss")
        XCTAssertTrue(dismiss.exists)
    }

    func testRealClaudeCodeMenuItems() throws {
        launchStub(script: Self.realClaudeCodeScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        let yes = button(label: "Yes")
        XCTAssertTrue(yes.exists, "Menu item 'Yes' should exist")

        // Item 2 has a long label; check it starts with the expected prefix
        let item2 = app.buttons.matching(
            NSPredicate(format: "label BEGINSWITH %@", "Yes, and don't ask again")
        ).firstMatch
        XCTAssertTrue(item2.exists, "Menu item 2 should exist")

        let no = button(label: "No")
        XCTAssertTrue(no.exists, "Menu item 'No' should exist")
    }

    func testRealClaudeCodePromptText() throws {
        launchStub(script: Self.realClaudeCodeScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        let prompt = app.staticTexts.matching(
            NSPredicate(format: "label CONTAINS 'Do you want to proceed'")
        )
        XCTAssertGreaterThan(prompt.count, 0,
                             "Prompt text should include 'Do you want to proceed'")
    }

    // MARK: - Recall Pill Tests

    func testRecallPillAppearsAfterDismiss() throws {
        launchStub(script: Self.numberedMenuScript, agent: "claude-code")
        waitForTerminal()
        waitForOverlay()

        // Dismiss the overlay
        let dismissButton = button(label: "Dismiss")
        tapButton(dismissButton)
        waitForOverlayDismissed()

        // Recall pill should appear
        let recallPill = app.buttons["recall-pill"]
        let deadline = Date().addingTimeInterval(5)
        while Date() < deadline {
            if recallPill.exists { break }
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
        }
        XCTAssertTrue(recallPill.exists, "Recall pill should appear after dismiss")

        // Tap the recall pill
        tapButton(recallPill)

        // Overlay should reappear
        waitForOverlay()

        // Recall pill should be gone
        XCTAssertFalse(app.buttons["recall-pill"].exists,
                       "Recall pill should disappear after recall")
    }

    // MARK: - Guard Tests

    func testOverlayNotShownWithoutAgent() throws {
        // Use a unique device ID to avoid persisted agent selection from prior test runs
        let noAgentConfig: String = {
            let config: [String: String] = [
                "productId": "pr-stub0000",
                "deviceId": "de-noagent0",
                "sct": "stubtoken",
                "fingerprint": "0000000000000000000000000000000000000000000000000000000000000000"
            ]
            let data = try! JSONSerialization.data(withJSONObject: config)
            return String(data: data, encoding: .utf8)!
        }()

        // Launch without --stub-agent so no agent is selected
        app.launchArguments = [
            "--stub-terminal",
            "--stub-script-b64", Self.numberedMenuScript,
            "--test-config", noAgentConfig
        ]
        app.launch()

        waitForTerminal()

        // Wait for scripted events to arrive
        RunLoop.current.run(until: Date().addingTimeInterval(2))

        let backdrop = app.otherElements["pattern-overlay-backdrop"]
        XCTAssertFalse(backdrop.exists, "Overlay should not appear when no agent is selected")
    }
}
