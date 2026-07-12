package com.localacr

enum class RecognitionErrorCode {
    DatabaseNotFound,
    DatabaseInvalid,
    UnsupportedSchema,
    UnsupportedFingerprintProfile,
    UnsupportedMatcherProfile,
    IntegrityCheckFailed,
    MicrophonePermissionDenied,
    MicrophoneUnavailable,
    AudioInterrupted,
    AudioDiscontinuity,
    AudioOverrun,
    AudioEngineFailure,
    QueryDensityExceeded,
    ResourceLimitExceeded,
    InvalidState,
    InvalidDatabasePath,
    InvalidConfig,
    NativeEngineFailure,
}

data class RecognitionError(
    val code: RecognitionErrorCode,
    val message: String,
    val recoverable: Boolean,
)

class CreateResult private constructor(
    val recognizer: LocalAcrRecognizer?,
    val error: RecognitionError?,
) {
    val isSuccess: Boolean = recognizer != null

    companion object {
        fun success(recognizer: LocalAcrRecognizer): CreateResult = CreateResult(recognizer, null)
        fun failure(error: RecognitionError): CreateResult = CreateResult(null, error)
    }
}

class PrepareResult private constructor(
    val error: RecognitionError?,
) {
    val isSuccess: Boolean = error == null

    companion object {
        fun success(): PrepareResult = PrepareResult(null)
        fun failure(error: RecognitionError): PrepareResult = PrepareResult(error)
    }
}

class OperationResult private constructor(
    val error: RecognitionError?,
) {
    val isSuccess: Boolean = error == null

    companion object {
        fun success(): OperationResult = OperationResult(null)
        fun failure(error: RecognitionError): OperationResult = OperationResult(error)
    }
}
