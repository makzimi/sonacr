import Foundation
import LocalAcrShared

public typealias LACRRecognitionListener = RecognitionListener
public typealias LACRRecognitionResult = RecognitionResult
public typealias LACRRecognitionError = RecognitionError

public enum LACRRecognizerFacadeError: Error, CustomStringConvertible {
    case creation(String)

    public var description: String {
        switch self {
        case .creation(let message):
            return message
        }
    }
}

public final class LACRRecognizer {
    private let recognizer: LocalAcrRecognizer

    public init(databasePath: String, duplicateCooldownMs: Int64 = 30_000) throws {
        let config = RecognitionConfig(duplicateCooldownMs: duplicateCooldownMs)
        let created = LocalAcrFactory.shared.create(databasePath: databasePath, config: config)
        guard let recognizer = created.recognizer else {
            throw LACRRecognizerFacadeError.creation(created.error?.message ?? "Unable to create recognizer")
        }
        self.recognizer = recognizer
    }

    public func prepare(_ completion: @escaping (PrepareResult) -> Void) {
        recognizer.prepare(completion: completion)
    }

    public func start(listener: LACRRecognitionListener, _ completion: @escaping (OperationResult) -> Void) {
        recognizer.start(listener: listener, completion: completion)
    }

    public func stop(_ completion: @escaping (OperationResult) -> Void) {
        recognizer.stop(completion: completion)
    }

    public func close(_ completion: @escaping (OperationResult) -> Void) {
        recognizer.close(completion: completion)
    }
}
