import Foundation

private let promotionCooldownMs: Int64 = 30_000

enum DemoListeningStatus: Equatable {
    case idle
    case permissionRequired
    case preparing
    case listening
    case error
}

struct DemoRecognition {
    let triggerId: String
    let displayName: String
    let confidence: Float
    let metadataJson: String
}

struct DemoPromotion: Equatable {
    let triggerId: String
    let title: String
    let body: String
    let cta: String
    let confidence: Float
}

struct DemoState: Equatable {
    var status: DemoListeningStatus = .idle
    var promotion: DemoPromotion?
    var lastRecognizedTriggerId: String?
    var localActionMessage: String?
    var errorMessage: String?
}

protocol DemoClock {
    func nowMilliseconds() -> Int64
}

struct SystemDemoClock: DemoClock {
    func nowMilliseconds() -> Int64 {
        Int64(ProcessInfo.processInfo.systemUptime * 1_000.0)
    }
}

enum DemoOperationResult: Equatable {
    case success
    case failure(String)
}

protocol DemoRecognitionListener: AnyObject {
    func recognized(_ recognition: DemoRecognition)
    func failed(message: String)
}

protocol DemoRecognizing: AnyObject {
    func prepare() -> DemoOperationResult
    func start(listener: DemoRecognitionListener) -> DemoOperationResult
    func stop() -> DemoOperationResult
}

final class DemoController: ObservableObject, DemoRecognitionListener {
    @Published private(set) var state: DemoState

    private let recognizer: DemoRecognizing
    private let clock: DemoClock
    private var lastPromotionByTriggerMs: [String: Int64] = [:]

    init(
        recognizer: DemoRecognizing,
        clock: DemoClock = SystemDemoClock(),
        initialState: DemoState = DemoState()
    ) {
        self.recognizer = recognizer
        self.clock = clock
        self.state = initialState
    }

    func screenVisible(permissionGranted: Bool) {
        guard permissionGranted else {
            state.status = .permissionRequired
            state.errorMessage = nil
            return
        }

        state.status = .preparing
        state.errorMessage = nil
        switch recognizer.prepare() {
        case .success:
            startListening()
        case .failure(let message):
            showError(message)
        }
    }

    func screenHidden() {
        _ = recognizer.stop()
        state.status = .idle
    }

    func promotionCta() {
        state.localActionMessage = "Claimed locally"
    }

    func dismissPromotion() {
        state.promotion = nil
    }

    func recognized(_ recognition: DemoRecognition) {
        let nowMs = clock.nowMilliseconds()
        if let previousMs = lastPromotionByTriggerMs[recognition.triggerId],
           nowMs - previousMs < promotionCooldownMs {
            return
        }
        lastPromotionByTriggerMs[recognition.triggerId] = nowMs

        let metadata = PromotionMetadata.parse(recognition.metadataJson)
        state.status = .listening
        state.promotion = DemoPromotion(
            triggerId: recognition.triggerId,
            title: metadata.title.isEmpty ? recognition.displayName : metadata.title,
            body: metadata.body.isEmpty ? "Recognized \(recognition.displayName)" : metadata.body,
            cta: metadata.cta.isEmpty ? "Show offer" : metadata.cta,
            confidence: recognition.confidence
        )
        state.lastRecognizedTriggerId = recognition.triggerId
        state.localActionMessage = nil
        state.errorMessage = nil
    }

    func failed(message: String) {
        showError(message)
    }

    private func startListening() {
        switch recognizer.start(listener: self) {
        case .success:
            state.status = .listening
            state.errorMessage = nil
        case .failure(let message):
            showError(message)
        }
    }

    private func showError(_ message: String) {
        state.status = .error
        state.errorMessage = message
    }
}

private struct PromotionMetadata {
    let title: String
    let body: String
    let cta: String

    static func parse(_ json: String) -> PromotionMetadata {
        guard let data = json.data(using: .utf8),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return PromotionMetadata(title: "", body: "", cta: "")
        }
        return PromotionMetadata(
            title: object["title"] as? String ?? "",
            body: object["body"] as? String ?? "",
            cta: object["cta"] as? String ?? ""
        )
    }
}
