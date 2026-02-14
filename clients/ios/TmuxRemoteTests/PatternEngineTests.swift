import XCTest
@testable import TmuxRemote

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

    func testDismissLocallyHidesOverlayButRetainsMatch() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))

        engine.dismissLocally(instanceId: "inst-1")

        XCTAssertNotNil(engine.activeMatch)
        XCTAssertNil(engine.visibleMatch)
        XCTAssertTrue(engine.canRestoreHiddenMatch)
    }

    func testRestoreOverlayShowsHiddenMatchAgain() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))
        engine.dismissLocally(instanceId: "inst-1")

        engine.restoreOverlay()

        XCTAssertNotNil(engine.visibleMatch)
        XCTAssertFalse(engine.canRestoreHiddenMatch)
    }

    func testNewPresentClearsHiddenState() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))
        engine.dismissLocally(instanceId: "inst-1")

        engine.applyServerPresent(makeMatch(instanceId: "inst-2", revision: 1))

        XCTAssertEqual(engine.activeMatch?.id, "inst-2")
        XCTAssertEqual(engine.visibleMatch?.id, "inst-2")
        XCTAssertFalse(engine.canRestoreHiddenMatch)
    }

    func testImmediatePresentAfterResolveIsIgnored() {
        let engine = makeEngine()
        nowRef = Date(timeIntervalSince1970: 100)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1, prompt: "Do you want to proceed?"))
        engine.dismissLocally(instanceId: "inst-1")
        engine.restoreOverlay()
        engine.resolveLocally(instanceId: "inst-1")

        nowRef = Date(timeIntervalSince1970: 100.1)
        engine.applyServerPresent(makeMatch(instanceId: "inst-2", revision: 1, prompt: "Do you want to proceed?"))

        XCTAssertNil(engine.visibleMatch)
    }

    func testImmediatePresentAfterGoneForSameInstanceIsIgnored() {
        let engine = makeEngine()
        nowRef = Date(timeIntervalSince1970: 300)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1, prompt: "Do you want to proceed?"))
        engine.applyServerGone(instanceId: "inst-1")

        nowRef = Date(timeIntervalSince1970: 300.1)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 2, prompt: "Do you want to proceed?"))

        XCTAssertNil(engine.visibleMatch)
        XCTAssertNil(engine.activeMatch)
    }

    func testPresentAfterGoneSuppressionWindowAppears() {
        let engine = makeEngine()
        nowRef = Date(timeIntervalSince1970: 400)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1, prompt: "Do you want to proceed?"))
        engine.applyServerGone(instanceId: "inst-1")

        nowRef = Date(timeIntervalSince1970: 402)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 2, prompt: "Do you want to proceed?"))

        XCTAssertNotNil(engine.visibleMatch)
        XCTAssertEqual(engine.visibleMatch?.id, "inst-1")
    }

    func testDifferentInstanceAfterGoneAppearsImmediately() {
        let engine = makeEngine()
        nowRef = Date(timeIntervalSince1970: 500)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1, prompt: "Do you want to proceed?"))
        engine.applyServerGone(instanceId: "inst-1")

        nowRef = Date(timeIntervalSince1970: 500.1)
        engine.applyServerPresent(makeMatch(instanceId: "inst-2", revision: 1, prompt: "Do you want to proceed?"))

        XCTAssertNotNil(engine.visibleMatch)
        XCTAssertEqual(engine.visibleMatch?.id, "inst-2")
    }

    func testPresentAfterResolveSuppressionWindowAppears() {
        let engine = makeEngine()
        nowRef = Date(timeIntervalSince1970: 200)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1, prompt: "Do you want to proceed?"))
        engine.resolveLocally(instanceId: "inst-1")

        nowRef = Date(timeIntervalSince1970: 202)
        engine.applyServerPresent(makeMatch(instanceId: "inst-2", revision: 1, prompt: "Do you want to proceed?"))

        XCTAssertNotNil(engine.visibleMatch)
        XCTAssertEqual(engine.visibleMatch?.id, "inst-2")
    }

    func testResetClearsActiveMatch() {
        let engine = makeEngine()
        engine.applyServerPresent(makeMatch())

        engine.reset()

        XCTAssertNil(engine.activeMatch)
        XCTAssertNil(engine.visibleMatch)
        XCTAssertFalse(engine.canRestoreHiddenMatch)
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

    func testGoneWithinDebounceEventuallyClears() {
        let engine = PatternEngine(minimumVisibleDuration: 0.05)
        engine.applyServerPresent(makeMatch(instanceId: "inst-1", revision: 1))

        engine.applyServerGone(instanceId: "inst-1")

        XCTAssertNotNil(engine.activeMatch)

        let deadline = Date().addingTimeInterval(1.0)
        while Date() < deadline {
            if engine.activeMatch == nil { break }
            RunLoop.current.run(until: Date().addingTimeInterval(0.01))
        }
        XCTAssertNil(engine.activeMatch)
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
