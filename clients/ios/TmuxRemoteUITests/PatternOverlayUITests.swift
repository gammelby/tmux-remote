import XCTest

final class PatternOverlayUITests: XCTestCase {

    private var app: XCUIApplication!

    private static func encodeScript(_ script: [String: Any]) -> String {
        let data = try! JSONSerialization.data(withJSONObject: script)
        return data.base64EncodedString()
    }

    private static let numberedMenuScript: String = encodeScript([
        "events": [
            [
                "type": "pattern_present",
                "delay": 0.1,
                "instance_id": "inst-1",
                "pattern_id": "numbered_prompt",
                "pattern_type": "numbered_menu",
                "revision": 1,
                "prompt": "Do you want to proceed?",
                "actions": [
                    ["label": "Yes", "keys": "1"],
                    ["label": "Yes, and don't ask again", "keys": "2"],
                    ["label": "No", "keys": "3"]
                ]
            ]
        ]
    ])

    private static let yesNoScript: String = encodeScript([
        "events": [
            [
                "type": "pattern_present",
                "delay": 0.1,
                "instance_id": "inst-yn",
                "pattern_id": "yes_no_prompt",
                "pattern_type": "yes_no",
                "revision": 1,
                "prompt": NSNull(),
                "actions": [
                    ["label": "Allow", "keys": "y"],
                    ["label": "Deny", "keys": "n"]
                ]
            ]
        ]
    ])

    private static let goneScript: String = encodeScript([
        "events": [
            [
                "type": "pattern_present",
                "delay": 0.1,
                "instance_id": "inst-gone",
                "pattern_id": "yes_no_prompt",
                "pattern_type": "yes_no",
                "revision": 1,
                "prompt": "Continue?",
                "actions": [
                    ["label": "Allow", "keys": "y"],
                    ["label": "Deny", "keys": "n"]
                ]
            ],
            [
                "type": "pattern_gone",
                "delay": 2.0,
                "instance_id": "inst-gone"
            ]
        ]
    ])

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

    override func setUpWithError() throws {
        continueAfterFailure = false
        app = XCUIApplication()
    }

    override func tearDown() {
        app?.terminate()
        app = nil
    }

    private func launchStub(script: String) {
        app.launchArguments = [
            "--stub-terminal",
            "--stub-script-b64", script,
            "--test-config", Self.testConfig
        ]
        app.launch()
    }

    @discardableResult
    private func waitForTerminal(timeout: TimeInterval = 10) -> XCUIElement {
        let pill = app.staticTexts["connection-pill"]
        XCTAssertTrue(pill.waitForExistence(timeout: timeout), "Terminal screen should appear")
        return pill
    }

    @discardableResult
    private func waitForOverlay(timeout: TimeInterval = 10) -> XCUIElement {
        let backdrop = app.otherElements["pattern-overlay-backdrop"]
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if backdrop.exists { return backdrop }
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
        }
        XCTFail("Pattern overlay should appear within \(timeout)s")
        return backdrop
    }

    private func waitForOverlayDismissed(timeout: TimeInterval = 5) {
        let backdrop = app.otherElements["pattern-overlay-backdrop"]
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if !backdrop.exists { return }
            RunLoop.current.run(until: Date().addingTimeInterval(0.2))
        }
        XCTFail("Pattern overlay should disappear within \(timeout)s")
    }

    @discardableResult
    private func waitForRecallPill(timeout: TimeInterval = 5) -> XCUIElement {
        let pill = app.buttons["pattern-recall-pill"]
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if pill.exists { return pill }
            RunLoop.current.run(until: Date().addingTimeInterval(0.2))
        }
        XCTFail("Recall pill should appear within \(timeout)s")
        return pill
    }

    private func button(label: String) -> XCUIElement {
        app.buttons.matching(NSPredicate(format: "label == %@", label)).firstMatch
    }

    private func tapButton(_ element: XCUIElement) {
        element.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)).tap()
    }

    func testNumberedMenuOverlayAppears() throws {
        launchStub(script: Self.numberedMenuScript)
        waitForTerminal()
        waitForOverlay()

        XCTAssertTrue(button(label: "Dismiss").exists)
        XCTAssertTrue(button(label: "Yes").exists)
        XCTAssertTrue(button(label: "No").exists)
    }

    func testTapMenuItemSendsKeys() throws {
        launchStub(script: Self.numberedMenuScript)
        waitForTerminal()
        waitForOverlay()

        let yes = button(label: "Yes")
        XCTAssertTrue(yes.exists)
        tapButton(yes)

        let sentKeys = app.staticTexts["debug-sent-keys"]
        let deadline = Date().addingTimeInterval(5)
        while Date() < deadline {
            if sentKeys.exists && !sentKeys.label.isEmpty { break }
            RunLoop.current.run(until: Date().addingTimeInterval(0.2))
        }
        XCTAssertEqual(sentKeys.label, "1")
        waitForOverlayDismissed()
    }

    func testDismissButtonClosesOverlay() throws {
        launchStub(script: Self.numberedMenuScript)
        waitForTerminal()
        waitForOverlay()

        let dismiss = button(label: "Dismiss")
        XCTAssertTrue(dismiss.exists)
        tapButton(dismiss)
        waitForOverlayDismissed()
        XCTAssertTrue(waitForRecallPill().exists)
    }

    func testDismissThenShowPromptRestoresOverlay() throws {
        launchStub(script: Self.numberedMenuScript)
        waitForTerminal()
        waitForOverlay()

        let dismiss = button(label: "Dismiss")
        XCTAssertTrue(dismiss.exists)
        tapButton(dismiss)
        waitForOverlayDismissed()

        let recall = waitForRecallPill()
        tapButton(recall)
        waitForOverlay()
    }

    func testYesNoOverlayAppears() throws {
        launchStub(script: Self.yesNoScript)
        waitForTerminal()
        waitForOverlay()

        XCTAssertTrue(button(label: "Allow").exists)
        XCTAssertTrue(button(label: "Deny").exists)
    }

    func testPatternGoneClosesOverlay() throws {
        launchStub(script: Self.goneScript)
        waitForTerminal()
        waitForOverlay()
        waitForOverlayDismissed(timeout: 6)
    }
}
