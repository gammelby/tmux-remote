import XCTest
import SwiftCBOR
@testable import NabtoShell

final class CBORHelpersTests: XCTestCase {

    // MARK: - Encoding

    func testEncodeAttach() {
        let data = CBORHelpers.encodeAttach(session: "main", cols: 80, rows: 24)
        let decoded = try! CBOR.decode([UInt8](data))!
        guard case .map(let map) = decoded else {
            XCTFail("Expected CBOR map")
            return
        }
        XCTAssertEqual(map[.utf8String("session")], .utf8String("main"))
        XCTAssertEqual(map[.utf8String("cols")], .unsignedInt(80))
        XCTAssertEqual(map[.utf8String("rows")], .unsignedInt(24))
    }

    func testEncodeCreateWithoutCommand() {
        let data = CBORHelpers.encodeCreate(session: "test", cols: 120, rows: 40)
        let decoded = try! CBOR.decode([UInt8](data))!
        guard case .map(let map) = decoded else {
            XCTFail("Expected CBOR map")
            return
        }
        XCTAssertEqual(map[.utf8String("session")], .utf8String("test"))
        XCTAssertEqual(map[.utf8String("cols")], .unsignedInt(120))
        XCTAssertEqual(map[.utf8String("rows")], .unsignedInt(40))
        XCTAssertNil(map[.utf8String("command")])
    }

    func testEncodeCreateWithCommand() {
        let data = CBORHelpers.encodeCreate(session: "test", cols: 80, rows: 24, command: "vim")
        let decoded = try! CBOR.decode([UInt8](data))!
        guard case .map(let map) = decoded else {
            XCTFail("Expected CBOR map")
            return
        }
        XCTAssertEqual(map[.utf8String("command")], .utf8String("vim"))
    }

    func testEncodeResize() {
        let data = CBORHelpers.encodeResize(cols: 132, rows: 50)
        let decoded = try! CBOR.decode([UInt8](data))!
        guard case .map(let map) = decoded else {
            XCTFail("Expected CBOR map")
            return
        }
        XCTAssertEqual(map.count, 2)
        XCTAssertEqual(map[.utf8String("cols")], .unsignedInt(132))
        XCTAssertEqual(map[.utf8String("rows")], .unsignedInt(50))
    }

    // MARK: - Decoding

    func testDecodeSessionsValid() {
        let cbor: CBOR = .array([
            .map([
                .utf8String("name"): .utf8String("main"),
                .utf8String("cols"): .unsignedInt(80),
                .utf8String("rows"): .unsignedInt(24),
                .utf8String("attached"): .unsignedInt(1)
            ])
        ])
        let data = Data(cbor.encode())
        let sessions = CBORHelpers.decodeSessions(from: data)
        XCTAssertEqual(sessions.count, 1)
        XCTAssertEqual(sessions[0].name, "main")
        XCTAssertEqual(sessions[0].cols, 80)
        XCTAssertEqual(sessions[0].rows, 24)
        XCTAssertEqual(sessions[0].attached, 1)
    }

    func testDecodeSessionsMissingOptionalFields() {
        let cbor: CBOR = .array([
            .map([
                .utf8String("name"): .utf8String("minimal")
            ])
        ])
        let data = Data(cbor.encode())
        let sessions = CBORHelpers.decodeSessions(from: data)
        XCTAssertEqual(sessions.count, 1)
        XCTAssertEqual(sessions[0].name, "minimal")
        XCTAssertEqual(sessions[0].cols, 0)
        XCTAssertEqual(sessions[0].rows, 0)
        XCTAssertEqual(sessions[0].attached, 0)
    }

    func testDecodeEmptyArray() {
        let cbor: CBOR = .array([])
        let data = Data(cbor.encode())
        let sessions = CBORHelpers.decodeSessions(from: data)
        XCTAssertTrue(sessions.isEmpty)
    }

    func testDecodeInvalidData() {
        let data = Data([0xFF, 0xFE, 0xFD])
        let sessions = CBORHelpers.decodeSessions(from: data)
        XCTAssertTrue(sessions.isEmpty)
    }
}
