package com.localacr

import com.localacr.internal.PlatformPorts
import com.localacr.internal.defaultPlatformPorts

data class RecognitionConfig(
    val duplicateCooldownMs: Long = 30_000,
)

data class RecognitionResult(
    val triggerId: String,
    val displayName: String,
    val confidence: Float,
    val matchedPositionMs: Long,
    val resultAgeMs: Long,
    val metadataJson: String,
)

enum class RecognitionState {
    Created,
    Preparing,
    Ready,
    Starting,
    Listening,
    Stopping,
    Failed,
    Closed,
}

interface RecognitionListener {
    fun onStateChanged(state: RecognitionState)
    fun onRecognized(result: RecognitionResult)
    fun onError(error: RecognitionError)
}

object LocalAcrFactory {
    fun create(databasePath: String): CreateResult =
        create(databasePath, RecognitionConfig(), defaultPlatformPorts())

    fun create(databasePath: String, config: RecognitionConfig): CreateResult =
        create(databasePath, config, defaultPlatformPorts())

    internal fun create(
        databasePath: String,
        config: RecognitionConfig = RecognitionConfig(),
        ports: PlatformPorts,
    ): CreateResult {
        val pathError = validateDatabasePath(databasePath)
        if (pathError != null) {
            return CreateResult.failure(pathError)
        }
        val configError = validateConfig(config)
        if (configError != null) {
            return CreateResult.failure(configError)
        }
        return CreateResult.success(LocalAcrRecognizer(databasePath, config, ports))
    }

    private fun validateDatabasePath(databasePath: String): RecognitionError? {
        if (databasePath.isBlank()) {
            return RecognitionError(
                RecognitionErrorCode.InvalidDatabasePath,
                "databasePath must be nonblank",
                recoverable = false,
            )
        }
        if (databasePath.any { it == '\u0000' }) {
            return RecognitionError(
                RecognitionErrorCode.InvalidDatabasePath,
                "databasePath must not contain NUL",
                recoverable = false,
            )
        }
        if (databasePath.encodeToByteArray().size > 4096) {
            return RecognitionError(
                RecognitionErrorCode.InvalidDatabasePath,
                "databasePath must be at most 4096 UTF-8 bytes",
                recoverable = false,
            )
        }
        return null
    }

    private fun validateConfig(config: RecognitionConfig): RecognitionError? {
        if (config.duplicateCooldownMs !in 0L..86_400_000L) {
            return RecognitionError(
                RecognitionErrorCode.InvalidConfig,
                "duplicateCooldownMs must be in 0..86400000",
                recoverable = false,
            )
        }
        return null
    }
}
