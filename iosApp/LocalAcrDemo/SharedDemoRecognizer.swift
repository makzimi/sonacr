import Foundation
import LocalAcrShared

final class SharedDemoRecognizer: NSObject, DemoRecognizing, RecognitionListener {
    private let databasePath: String
    private var recognizer: LocalAcrRecognizer?
    private weak var listener: DemoRecognitionListener?
    private var prepared = false

    init(databasePath: String) {
        self.databasePath = databasePath
    }

    func prepare() -> DemoOperationResult {
        if prepared {
            return .success
        }
        let created = LocalAcrFactory.shared.create(databasePath: databasePath)
        guard let nextRecognizer = created.recognizer else {
            return .failure(created.error?.message ?? "Unable to create recognizer")
        }
        recognizer = nextRecognizer
        nextRecognizer.prepare { [weak self] result in
            guard let self else { return }
            if let error = result.error {
                self.listener?.failed(message: error.message)
                return
            }
            self.prepared = true
            if let listener = self.listener {
                let startResult = self.startPreparedRecognizer(listener: listener)
                if case .failure(let message) = startResult {
                    listener.failed(message: message)
                }
            }
        }
        return .success
    }

    func start(listener: DemoRecognitionListener) -> DemoOperationResult {
        self.listener = listener
        guard prepared else {
            return .success
        }
        return startPreparedRecognizer(listener: listener)
    }

    func stop() -> DemoOperationResult {
        listener = nil
        recognizer?.stop { _ in }
        return .success
    }

    func onStateChanged(state: RecognitionState) {
    }

    func onRecognized(result: RecognitionResult) {
        listener?.recognized(
            DemoRecognition(
                triggerId: result.triggerId,
                displayName: result.displayName,
                confidence: result.confidence,
                metadataJson: result.metadataJson
            )
        )
    }

    func onError(error: RecognitionError) {
        listener?.failed(message: error.message)
    }

    private func startPreparedRecognizer(listener: DemoRecognitionListener) -> DemoOperationResult {
        guard let recognizer else {
            return .failure("Recognizer is not prepared")
        }
        var operationError: RecognitionError?
        recognizer.start(listener: self) { result in
            operationError = result.error
        }
        if let operationError {
            return .failure(operationError.message)
        }
        return .success
    }
}
