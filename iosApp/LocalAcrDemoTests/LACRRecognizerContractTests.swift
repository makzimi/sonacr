import XCTest
@testable import LocalAcrDemo

final class LACRRecognizerContractTests: XCTestCase {
    func testFacadeRejectsInvalidCooldownWithTypedError() {
        XCTAssertThrowsError(
            try LACRRecognizer(databasePath: "/tmp/missing.lacrdb", duplicateCooldownMs: -1)
        ) { error in
            XCTAssertTrue(String(describing: error).contains("duplicateCooldownMs"))
        }
    }

    func testFacadeCanBeConstructedWithSpecSurface() throws {
        let recognizer = try LACRRecognizer(databasePath: "/tmp/missing.lacrdb", duplicateCooldownMs: 30_000)
        XCTAssertNotNil(recognizer)
    }
}
