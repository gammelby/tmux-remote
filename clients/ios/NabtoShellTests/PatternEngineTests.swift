import XCTest
@testable import NabtoShell

final class PatternEngineTests: XCTestCase {
    private var nowRef = Date(timeIntervalSince1970: 0)

    private func makeEngine(minVisible: TimeInterval = 0) -> PatternEngine {
        PatternEngine(minimumVisibleDuration: minVisible, now: { self.nowRef })
    }

    private func makeMatch(
        instanceId: String = "inst-1",
        revision: Int = 1,
        prompt: String? = "Continue?"
    ) -> PatternMatch {
        PatternMatch(
            id: instanceId,
            patternId: "yes_no_prompt",
            patternType: .yesNo,
            prompt: prompt,
            actions: [
                ResolvedAction(label: "Allow", keys: "y"),
                ResolvedAction(label: "Deny", keys: "n")
            ],
            revision: revision
        )
    }

    func testPresentSetsActiveMatch() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch())

        XCTAssertNotNil(engine.activeMatch)
        XCTAssertEqual(engine.activeMatch?.id, "inst-1")
    }

    func testUpdateReplacesMatchingInstance() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))

        engine.applyServerUpdate(makeMatch(instanceId: "inst-1", revision: 2, prompt: "Updated"))

        XCTAssertEqual(engine.activeMatch?.id, "inst-1")
        XCTAssertEqual(engine.activeMatch?.revision, 2)
        XCTAssertEqual(engine.activeMatch?.prompt, "Updated")
    }

    func testUpdateIgnoresDifferentInstance() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))

        engine.applyServerUpdate(makeMatch(instanceId: "inst-2", revision: 2, prompt: "Other"))

        XCTAssertEqual(engine.activeMatch?.id, "inst-1")
        XCTAssertEqual(engine.activeMatch?.revision, 1)
    }

    func testGoneClearsMatchingInstance() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))

        engine.applyServerGone(instanceId: "inst-1")

        XCTAssertNil(engine.activeMatch)
    }

    func testGoneIgnoresDifferentInstance() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))

        engine.applyServerGone(instanceId: "inst-2")

        XCTAssertNotNil(engine.activeMatch)
        XCTAssertEqual(engine.activeMatch?.id, "inst-1")
    }

    func testResolveLocallyClearsMatchingInstance() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))

        engine.resolveLocally(instanceId: "inst-1")

        XCTAssertNil(engine.activeMatch)
    }

    func testResetClearsActiveMatch() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch())

        engine.reset()

        XCTAssertNil(engine.activeMatch)
    }

    func testGoneIsDebouncedImmediatelyAfterPresent() {
        let engine = makeEngine(minVisible: 1.0)
        nowRef = Date(timeIntervalSince1970: 10)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))

        nowRef = Date(timeIntervalSince1970: 10.2)
        engine.applyServerGone(instanceId: "inst-1")

        XCTAssertNotNil(engine.activeMatch)
        XCTAssertEqual(engine.activeMatch?.id, "inst-1")
    }

    func testGoneClearsAfterDebounceWindow() {
        let engine = makeEngine(minVisible: 1.0)
        nowRef = Date(timeIntervalSince1970: 20)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))

        nowRef = Date(timeIntervalSince1970: 21.5)
        engine.applyServerGone(instanceId: "inst-1")

        XCTAssertNil(engine.activeMatch)
    }
}
