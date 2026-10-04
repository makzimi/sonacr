package com.localacr.demo

import com.localacr.RecognitionError
import com.localacr.RecognitionResult

private const val MaxAutomaticRestarts = 3

enum class DemoListeningStatus {
    Idle,
    PermissionRequired,
    Preparing,
    Listening,
    Error,
}

data class NowPlaying(
    val triggerId: String,
    val title: String,
    val artist: String,
    val confidence: Float,
    val matchedPositionMs: Long,
)

data class DemoState(
    val status: DemoListeningStatus = DemoListeningStatus.Idle,
    val nowPlaying: NowPlaying? = null,
    val errorMessage: String? = null,
)

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
    private val catalog: TrackCatalog = TrackCatalog(emptyMap()),
    initialState: DemoState = DemoState(),
    private val onStateChanged: (DemoState) -> Unit = {},
) : DemoRecognitionListener {
    var state: DemoState = initialState
        private set

    private var automaticRestarts = 0

    fun onScreenVisible(permissionGranted: Boolean) {
        val status = if (permissionGranted) DemoListeningStatus.Idle else DemoListeningStatus.PermissionRequired
        setState(state.copy(status = status, errorMessage = null))
    }

    fun onStartListening(permissionGranted: Boolean) {
        if (!permissionGranted) {
            setState(state.copy(status = DemoListeningStatus.PermissionRequired, errorMessage = null))
            return
        }

        automaticRestarts = 0
        setState(state.copy(status = DemoListeningStatus.Preparing, nowPlaying = null, errorMessage = null))
        when (val prepareResult = recognizer.prepare()) {
            DemoOperationResult.Success -> startListening()
            is DemoOperationResult.Failure -> showError(prepareResult.message)
        }
    }

    fun onStopListening() {
        recognizer.stop()
        setState(state.copy(status = DemoListeningStatus.Idle))
    }

    fun onScreenHidden() {
        onStopListening()
    }

    override fun onRecognized(result: RecognitionResult) {
        if (!isActive()) {
            return
        }
        automaticRestarts = 0
        val track = catalog.lookup(result.triggerId)
        setState(
            state.copy(
                status = DemoListeningStatus.Listening,
                nowPlaying = NowPlaying(
                    triggerId = result.triggerId,
                    title = track.title,
                    artist = track.artist,
                    confidence = result.confidence,
                    matchedPositionMs = result.matchedPositionMs,
                ),
                errorMessage = null,
            ),
        )
    }

    override fun onError(error: RecognitionError) {
        if (!isActive()) {
            return
        }
        if (state.status == DemoListeningStatus.Listening && automaticRestarts < MaxAutomaticRestarts) {
            automaticRestarts += 1
            startListening()
            return
        }
        showError(error.message)
    }

    private fun startListening() {
        when (val startResult = recognizer.start(this)) {
            DemoOperationResult.Success ->
                setState(state.copy(status = DemoListeningStatus.Listening, errorMessage = null))
            is DemoOperationResult.Failure -> showError(startResult.message)
        }
    }

    private fun isActive(): Boolean =
        state.status == DemoListeningStatus.Preparing || state.status == DemoListeningStatus.Listening

    private fun showError(message: String) {
        setState(state.copy(status = DemoListeningStatus.Error, errorMessage = message))
    }

    private fun setState(nextState: DemoState) {
        state = nextState
        onStateChanged(nextState)
    }
}
