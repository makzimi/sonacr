import XCTest
@testable import LocalAcrDemo

final class DemoControllerTests: XCTestCase {
    func testWaitsForPermissionBeforeListening() {
        let recognizer = FakeDemoRecognizer()
        let controller = DemoController(recognizer: recognizer)

        controller.screenVisible(permissionGranted: false)

        XCTAssertEqual(controller.state.status, .permissionRequired)
        XCTAssertFalse(recognizer.started)
    }

    func testStartsWhenPermissionGranted() {
        let recognizer = FakeDemoRecognizer()
        let controller = DemoController(recognizer: recognizer)

        controller.screenVisible(permissionGranted: true)

        XCTAssertEqual(controller.state.status, .listening)
        XCTAssertTrue(recognizer.prepared)
        XCTAssertTrue(recognizer.started)
    }

    func testRecognitionShowsPromotionFromMetadata() {
        let recognizer = FakeDemoRecognizer()
        let controller = DemoController(recognizer: recognizer)
        controller.screenVisible(permissionGranted: true)

        recognizer.emit(
            triggerId: "checkin",
            metadataJson: #"{"title":"20% off","body":"Show this today","cta":"Claim"}"#
        )

        XCTAssertEqual(controller.state.promotion?.title, "20% off")
        XCTAssertEqual(controller.state.promotion?.body, "Show this today")
        XCTAssertEqual(controller.state.promotion?.cta, "Claim")
        XCTAssertEqual(controller.state.lastRecognizedTriggerId, "checkin")
    }

    func testRepeatedTriggerWithinCooldownDoesNotReplacePromotion() {
        let recognizer = FakeDemoRecognizer()
        let clock = FakeClock()
        let controller = DemoController(recognizer: recognizer, clock: clock)
        controller.screenVisible(permissionGranted: true)

        recognizer.emit(triggerId: "same", metadataJson: #"{"title":"First","body":"Body","cta":"Open"}"#)
        clock.nowMs = 1_000
        recognizer.emit(triggerId: "same", metadataJson: #"{"title":"Second","body":"Body","cta":"Open"}"#)
        recognizer.emit(triggerId: "other", metadataJson: #"{"title":"Other","body":"Body","cta":"Open"}"#)

        XCTAssertEqual(controller.state.promotion?.title, "Other")
    }

    func testLocalCtaDismissAndStopAreLocalOnly() {
        let recognizer = FakeDemoRecognizer()
        let controller = DemoController(recognizer: recognizer)
        controller.screenVisible(permissionGranted: true)
        recognizer.emit(triggerId: "same", metadataJson: #"{"title":"First","body":"Body","cta":"Open"}"#)

        controller.promotionCta()
        XCTAssertEqual(controller.state.localActionMessage, "Claimed locally")
        controller.dismissPromotion()
        XCTAssertNil(controller.state.promotion)
        controller.screenHidden()
        XCTAssertTrue(recognizer.stopped)
        XCTAssertEqual(controller.state.status, .idle)
    }
}

private final class FakeClock: DemoClock {
    var nowMs: Int64 = 0
    func nowMilliseconds() -> Int64 { nowMs }
}

private final class FakeDemoRecognizer: DemoRecognizing {
    var listener: DemoRecognitionListener?
    var prepared = false
    var started = false
    var stopped = false

    func prepare() -> DemoOperationResult {
        prepared = true
        return .success
    }

    func start(listener: DemoRecognitionListener) -> DemoOperationResult {
        self.listener = listener
        started = true
        return .success
    }

    func stop() -> DemoOperationResult {
        stopped = true
        started = false
        return .success
    }

    func emit(triggerId: String, metadataJson: String) {
        listener?.recognized(
            DemoRecognition(
                triggerId: triggerId,
                displayName: triggerId,
                confidence: 0.9,
                metadataJson: metadataJson
            )
        )
    }
}
