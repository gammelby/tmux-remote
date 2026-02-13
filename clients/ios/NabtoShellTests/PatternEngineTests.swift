import XCTest
@testable import NabtoShell

final class PatternEngineTests: XCTestCase {

    private func makeConfig() -> PatternConfig {
        PatternConfig(version: 1, agents: [
            "test": AgentConfig(name: "Test", patterns: [
                PatternDefinition(
                    id: "yn",
                    type: .yesNo,
                    regex: "Continue\\? \\(y/n\\)",
                    multiLine: nil,
                    actions: [
                        PatternAction(label: "Yes", keys: "y"),
                        PatternAction(label: "No", keys: "n")
                    ],
                    actionTemplate: nil
                )
            ])
        ])
    }

    private func makeMatch(id: String = "yn", prompt: String? = "Continue?") -> PatternMatch {
        PatternMatch(
            id: id, patternType: .yesNo, prompt: prompt,
            matchedText: "Continue? (y/n)",
            actions: [ResolvedAction(label: "Yes", keys: "y")],
            matchPosition: 0
        )
    }

    override func tearDown() {
        // Clean up any persisted test keys
        let defaults = UserDefaults.standard
        for key in defaults.dictionaryRepresentation().keys where key.hasPrefix("patternAgent_test-") {
            defaults.removeObject(forKey: key)
        }
        super.tearDown()
    }

    // MARK: - Agent selection

    func testSelectAgent() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")
        XCTAssertEqual(engine.activeAgent, "test")
    }

    func testSelectAgentOff() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")
        engine.selectAgent(nil)
        XCTAssertNil(engine.activeAgent)
    }

    func testSelectAgentClearsMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")
        engine.applyServerMatch(makeMatch())
        XCTAssertNotNil(engine.activeMatch)

        engine.selectAgent(nil)
        XCTAssertNil(engine.activeMatch)
    }

    func testAvailableAgents() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())

        let agents = engine.availableAgents
        XCTAssertEqual(agents.count, 1)
        XCTAssertEqual(agents[0].id, "test")
        XCTAssertEqual(agents[0].name, "Test")
    }

    func testAgentSwitching() {
        let config = PatternConfig(version: 1, agents: [
            "a": AgentConfig(name: "Agent A", patterns: []),
            "b": AgentConfig(name: "Agent B", patterns: [])
        ])

        let engine = PatternEngine()
        engine.loadConfig(config)
        engine.selectAgent("a")

        engine.applyServerMatch(makeMatch(id: "pa"))
        XCTAssertEqual(engine.activeMatch?.id, "pa")

        engine.selectAgent("b")
        XCTAssertNil(engine.activeMatch)
    }

    // MARK: - Persistence

    func testPersistAgentSelection() {
        let deviceId = "test-device-persist"
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.setDeviceId(deviceId)
        engine.selectAgent("test")

        let saved = UserDefaults.standard.string(forKey: "patternAgent_\(deviceId)")
        XCTAssertEqual(saved, "test")
    }

    func testRestoreAgentSelection() {
        let deviceId = "test-device-restore"
        UserDefaults.standard.set("test", forKey: "patternAgent_\(deviceId)")

        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.setDeviceId(deviceId)

        XCTAssertEqual(engine.activeAgent, "test")
    }

    func testPersistAgentOff() {
        let deviceId = "test-device-off"
        UserDefaults.standard.set("test", forKey: "patternAgent_\(deviceId)")

        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.setDeviceId(deviceId)
        XCTAssertEqual(engine.activeAgent, "test")

        engine.selectAgent(nil)
        let saved = UserDefaults.standard.string(forKey: "patternAgent_\(deviceId)")
        XCTAssertNil(saved)
    }

    // MARK: - Server-push tests

    func testServerMatchSetsActiveMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        XCTAssertNotNil(engine.activeMatch)
        XCTAssertEqual(engine.activeMatch?.id, "yn")
    }

    func testServerMatchWithoutAgentIgnored() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())

        engine.applyServerMatch(makeMatch())
        XCTAssertNil(engine.activeMatch)
    }

    func testServerDismissClearsMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        XCTAssertNotNil(engine.activeMatch)

        engine.applyServerDismiss()
        XCTAssertNil(engine.activeMatch)

        // A new server match should work (no user dismiss happened)
        engine.applyServerMatch(makeMatch())
        XCTAssertNotNil(engine.activeMatch)
    }

    func testServerMatchRespectsDismissed() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        XCTAssertNotNil(engine.activeMatch)

        engine.dismiss()
        XCTAssertNil(engine.activeMatch)

        // Server pushes same match again (agent oscillation)
        engine.applyServerMatch(makeMatch())
        XCTAssertNil(engine.activeMatch, "Server match must be suppressed after user dismiss")
    }

    func testServerDismissDoesNotClearUserDismiss() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        engine.dismiss()

        // Server sends dismiss (agent auto-dismissed the same prompt)
        engine.applyServerDismiss()

        // Server immediately re-matches (oscillation)
        engine.applyServerMatch(makeMatch())
        XCTAssertNil(engine.activeMatch, "Oscillating server match must stay suppressed after user dismiss")
    }

    func testServerMatchAllowedAfterDebounce() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())

        // Dismiss in the past (>2s ago) to simulate debounce expiry
        engine.dismiss()
        engine.testSetUserDismissTime(Date().addingTimeInterval(-3.0))

        // Server pushes a new match: debounce expired, should be accepted
        engine.applyServerMatch(makeMatch(prompt: "Continue again?"))
        XCTAssertNotNil(engine.activeMatch, "Server match should be accepted after debounce expires")
    }

    // MARK: - Reset

    func testReset() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        XCTAssertNotNil(engine.activeMatch)

        engine.reset()
        XCTAssertNil(engine.activeMatch)
    }

    func testManualDismiss() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        XCTAssertNotNil(engine.activeMatch)

        engine.dismiss()
        XCTAssertNil(engine.activeMatch)
    }

    // MARK: - Recall / lastMatch

    func testDismissSavesLastMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        engine.dismiss()

        XCTAssertNil(engine.activeMatch)
        XCTAssertNotNil(engine.lastMatch)
        XCTAssertEqual(engine.lastMatch?.id, "yn")
    }

    func testRecallRestoresLastMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        engine.dismiss()
        XCTAssertNil(engine.activeMatch)

        engine.recall()
        XCTAssertNotNil(engine.activeMatch)
        XCTAssertEqual(engine.activeMatch?.id, "yn")
        XCTAssertNil(engine.lastMatch)
    }

    func testRecallNoOpWhenNoLastMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.recall()
        XCTAssertNil(engine.activeMatch)
        XCTAssertNil(engine.lastMatch)
    }

    func testServerMatchClearsLastMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        engine.dismiss()
        XCTAssertNotNil(engine.lastMatch)

        // Expire the 2s debounce so the next server match is accepted
        engine.testSetUserDismissTime(Date().addingTimeInterval(-3.0))

        engine.applyServerMatch(makeMatch(id: "new", prompt: "New prompt?"))
        XCTAssertNil(engine.lastMatch)
        XCTAssertEqual(engine.activeMatch?.id, "new")
    }

    func testServerDismissPreservesLastMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        engine.dismiss()
        XCTAssertNotNil(engine.lastMatch)

        // Server dismiss should NOT clear lastMatch: the user can still recall
        engine.applyServerDismiss()
        XCTAssertNotNil(engine.lastMatch, "Server dismiss should preserve lastMatch for recall")
    }

    func testResetClearsLastMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        engine.dismiss()
        XCTAssertNotNil(engine.lastMatch)

        engine.reset()
        XCTAssertNil(engine.lastMatch)
    }

    func testSelectAgentNilClearsLastMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        engine.dismiss()
        XCTAssertNotNil(engine.lastMatch)

        engine.selectAgent(nil)
        XCTAssertNil(engine.lastMatch)
    }

    // MARK: - Consume (action taken)

    func testConsumeDoesNotSaveLastMatch() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        XCTAssertNotNil(engine.activeMatch)

        engine.consume()
        XCTAssertNil(engine.activeMatch)
        XCTAssertNil(engine.lastMatch, "consume() should not save to lastMatch")
    }

    func testConsumeDoesNotDebounce() {
        let engine = PatternEngine()
        engine.loadConfig(makeConfig())
        engine.selectAgent("test")

        engine.applyServerMatch(makeMatch())
        engine.consume()

        // Next server match should be accepted immediately (no debounce)
        engine.applyServerMatch(makeMatch(id: "next", prompt: "Next prompt?"))
        XCTAssertNotNil(engine.activeMatch, "Server match after consume should not be debounced")
        XCTAssertEqual(engine.activeMatch?.id, "next")
    }
}
