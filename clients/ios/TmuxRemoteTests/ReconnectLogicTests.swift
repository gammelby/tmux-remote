import XCTest
@testable import TmuxRemote

final class ReconnectLogicTests: XCTestCase {

    func testBackoffSequence() {
        let logic = ReconnectLogic()
        XCTAssertEqual(logic.backoff(attempt: 1), 1, accuracy: 0.01)
        XCTAssertEqual(logic.backoff(attempt: 2), 2, accuracy: 0.01)
        XCTAssertEqual(logic.backoff(attempt: 3), 4, accuracy: 0.01)
        XCTAssertEqual(logic.backoff(attempt: 4), 8, accuracy: 0.01)
        XCTAssertEqual(logic.backoff(attempt: 5), 15, accuracy: 0.01) // capped at maxBackoff
        XCTAssertEqual(logic.backoff(attempt: 10), 15, accuracy: 0.01)
    }

    func testShouldNotGiveUpEarly() {
        let logic = ReconnectLogic()
        XCTAssertFalse(logic.shouldGiveUp(elapsedTime: 0))
        XCTAssertFalse(logic.shouldGiveUp(elapsedTime: 10))
        XCTAssertFalse(logic.shouldGiveUp(elapsedTime: 29))
        XCTAssertFalse(logic.shouldGiveUp(elapsedTime: 30))
    }

    func testShouldGiveUpAfterMaxTime() {
        let logic = ReconnectLogic()
        XCTAssertTrue(logic.shouldGiveUp(elapsedTime: 31))
        XCTAssertTrue(logic.shouldGiveUp(elapsedTime: 60))
    }
}
