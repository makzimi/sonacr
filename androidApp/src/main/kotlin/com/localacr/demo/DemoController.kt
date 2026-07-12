package com.localacr.demo

import com.localacr.RecognitionError
import com.localacr.RecognitionResult

private const val PromotionCooldownMs = 30_000L

enum class DemoListeningStatus {
    Idle,
    PermissionRequired,
    Preparing,
    Listening,
    Error,
}

data class DemoPromotion(
    val triggerId: String,
    val title: String,
    val body: String,
    val cta: String,
    val confidence: Float,
)

data class DemoState(
    val status: DemoListeningStatus = DemoListeningStatus.Idle,
    val promotion: DemoPromotion? = null,
    val lastRecognizedTriggerId: String? = null,
    val localActionMessage: String? = null,
    val errorMessage: String? = null,
)

interface DemoClock {
    fun nowMs(): Long
}

object SystemDemoClock : DemoClock {
    override fun nowMs(): Long = System.nanoTime() / 1_000_000L
}

sealed class DemoOperationResult {
    data object Success : DemoOperationResult()
    data class Failure(val message: String) : DemoOperationResult()
}

interface DemoRecognitionListener {
    fun onRecognized(result: RecognitionResult)
    fun onError(error: RecognitionError)
}

interface DemoRecognizer {
    fun prepare(): DemoOperationResult
    fun start(listener: DemoRecognitionListener): DemoOperationResult
    fun stop(): DemoOperationResult
}

class DemoController(
    private val recognizer: DemoRecognizer,
    private val clock: DemoClock = SystemDemoClock,
    initialState: DemoState = DemoState(),
    private val onStateChanged: (DemoState) -> Unit = {},
) : DemoRecognitionListener {
    var state: DemoState = initialState
        private set

    private val lastPromotionByTriggerMs = mutableMapOf<String, Long>()

    fun onScreenVisible(permissionGranted: Boolean) {
        if (!permissionGranted) {
            setState(state.copy(status = DemoListeningStatus.PermissionRequired, errorMessage = null))
            return
        }

        setState(state.copy(status = DemoListeningStatus.Preparing, errorMessage = null))
        when (val prepareResult = recognizer.prepare()) {
            DemoOperationResult.Success -> startListening()
            is DemoOperationResult.Failure -> showError(prepareResult.message)
        }
    }

    fun onScreenHidden() {
        recognizer.stop()
        setState(state.copy(status = DemoListeningStatus.Idle))
    }

    fun onPromotionCta() {
        setState(state.copy(localActionMessage = "Claimed locally"))
    }

    fun onDismissPromotion() {
        setState(state.copy(promotion = null))
    }

    override fun onRecognized(result: RecognitionResult) {
        val nowMs = clock.nowMs()
        val previousMs = lastPromotionByTriggerMs[result.triggerId]
        if (previousMs != null && nowMs - previousMs < PromotionCooldownMs) {
            return
        }
        lastPromotionByTriggerMs[result.triggerId] = nowMs

        val metadata = PromotionMetadata.parse(result.metadataJson)
        setState(
            state.copy(
                status = DemoListeningStatus.Listening,
                promotion = DemoPromotion(
                    triggerId = result.triggerId,
                    title = metadata.title.ifBlank { result.displayName },
                    body = metadata.body.ifBlank { "Recognized ${result.displayName}" },
                    cta = metadata.cta.ifBlank { "Show offer" },
                    confidence = result.confidence,
                ),
                lastRecognizedTriggerId = result.triggerId,
                localActionMessage = null,
                errorMessage = null,
            ),
        )
    }

    override fun onError(error: RecognitionError) {
        showError(error.message)
    }

    private fun startListening() {
        when (val startResult = recognizer.start(this)) {
            DemoOperationResult.Success ->
                setState(state.copy(status = DemoListeningStatus.Listening, errorMessage = null))
            is DemoOperationResult.Failure -> showError(startResult.message)
        }
    }

    private fun showError(message: String) {
        setState(state.copy(status = DemoListeningStatus.Error, errorMessage = message))
    }

    private fun setState(nextState: DemoState) {
        state = nextState
        onStateChanged(nextState)
    }
}

private data class PromotionMetadata(
    val title: String = "",
    val body: String = "",
    val cta: String = "",
) {
    companion object {
        fun parse(json: String): PromotionMetadata =
            PromotionMetadata(
                title = json.readJsonString("title"),
                body = json.readJsonString("body"),
                cta = json.readJsonString("cta"),
            )
    }
}

private fun String.readJsonString(name: String): String {
    val pattern = Regex("\"${Regex.escape(name)}\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"")
    return pattern.find(this)?.groupValues?.get(1)?.unescapeJsonString().orEmpty()
}

private fun String.unescapeJsonString(): String =
    replace("\\\"", "\"")
        .replace("\\\\", "\\")
        .replace("\\n", "\n")
        .replace("\\r", "\r")
        .replace("\\t", "\t")
