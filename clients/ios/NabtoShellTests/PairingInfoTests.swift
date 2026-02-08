import XCTest
@testable import NabtoShell

final class PairingInfoTests: XCTestCase {

    func testParseValidFullString() {
        let info = PairingInfo.parse("p=pr-xxx,d=de-yyy,u=admin,pwd=secret,sct=tok")
        XCTAssertNotNil(info)
        XCTAssertEqual(info?.productId, "pr-xxx")
        XCTAssertEqual(info?.deviceId, "de-yyy")
        XCTAssertEqual(info?.username, "admin")
        XCTAssertEqual(info?.password, "secret")
        XCTAssertEqual(info?.sct, "tok")
    }

    func testDefaultUsername() {
        let info = PairingInfo.parse("p=pr-xxx,d=de-yyy,pwd=secret,sct=tok")
        XCTAssertNotNil(info)
        XCTAssertEqual(info?.username, "owner")
    }

    func testMissingProductId() {
        let info = PairingInfo.parse("d=de-yyy,pwd=secret,sct=tok")
        XCTAssertNil(info)
    }

    func testMissingPassword() {
        let info = PairingInfo.parse("p=pr-xxx,d=de-yyy,sct=tok")
        XCTAssertNil(info)
    }

    func testMissingSCT() {
        let info = PairingInfo.parse("p=pr-xxx,d=de-yyy,pwd=secret")
        XCTAssertNil(info)
    }

    func testEmptyString() {
        XCTAssertNil(PairingInfo.parse(""))
        XCTAssertNil(PairingInfo.parse("   "))
    }

    func testExtraUnknownFields() {
        let info = PairingInfo.parse("p=pr-xxx,d=de-yyy,pwd=s,sct=t,x=ignored")
        XCTAssertNotNil(info)
        XCTAssertEqual(info?.productId, "pr-xxx")
        XCTAssertEqual(info?.deviceId, "de-yyy")
        XCTAssertEqual(info?.password, "s")
        XCTAssertEqual(info?.sct, "t")
    }
}
