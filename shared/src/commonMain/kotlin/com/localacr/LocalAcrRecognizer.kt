package com.localacr

import com.localacr.internal.CapturePort
import com.localacr.internal.NativeEvent
import com.localacr.internal.NativeSessionPort
import com.localacr.internal.PlatformPorts

class LocalAcrRecognizer internal constructor(
    private val databasePath: String,
    private val config: RecognitionConfig,
    private val ports: PlatformPorts,
) {
    var state: RecognitionState = RecognitionState.Created
        private set

    private var nativeSession: NativeSessionPort? = null
    private var capture: CapturePort? = null
    private var activeListener: RecognitionListener? = null
    private var sessionGeneration: Long = 0
    private var controlEpoch: Long = 0
    private var runtimeErrorDelivered = false
    private val cooldownByTrigger = mutableMapOf<String, Long>()

    fun prepare(completion: (PrepareResult) -> Unit) {
        if (state != RecognitionState.Created) {
            completePrepare(completion, invalidState("prepare requires Created"))
            return
        }
        state = RecognitionState.Preparing
        val session = ports.openNativeSession(databasePath, config)
        val error = session.prepare()
        if (error != null) {
            state = RecognitionState.Failed
            nativeSession = session
            completePrepare(completion, error)
            return
        }
        nativeSession = session
        state = RecognitionState.Ready
        completePrepare(completion, null)
    }

    fun start(listener: RecognitionListener, completion: (OperationResult) -> Unit) {
        if (state != RecognitionState.Ready) {
            completeOperation(completion, invalidState("start requires Ready"))
            return
        }
        val session = nativeSession
        if (session == null) {
            completeOperation(completion, invalidState("recognizer is not prepared"))
            return
        }

        state = RecognitionState.Starting
        dispatchState(listener, RecognitionState.Starting)
        val generation = ++sessionGeneration
        runtimeErrorDelivered = false
        cooldownByTrigger.clear()
        val startError = session.start { event -> onNativeEvent(generation, event) }
        if (startError != null) {
            ++sessionGeneration
            state = RecognitionState.Ready
            dispatchState(listener, RecognitionState.Ready)
            completeOperation(completion, startError)
            return
        }

        val nextCapture = ports.openCapture(session)
        val captureError = nextCapture.start()
        if (captureError != null) {
            session.stop()
            nextCapture.stop()
            ++sessionGeneration
            state = RecognitionState.Ready
            dispatchState(listener, RecognitionState.Ready)
            completeOperation(completion, captureError)
            return
        }

        capture = nextCapture
        activeListener = listener
        state = RecognitionState.Listening
        dispatchState(listener, RecognitionState.Listening)
        completeOperation(completion, null)
    }

    fun stop(completion: (OperationResult) -> Unit) {
        when (state) {
            RecognitionState.Closed -> {
                completeOperation(completion, null)
                return
            }
            RecognitionState.Ready, RecognitionState.Created, RecognitionState.Failed -> {
                completeOperation(completion, null)
                return
            }
            else -> Unit
        }

        val listener = activeListener
        state = RecognitionState.Stopping
        if (listener != null) {
            dispatchState(listener, RecognitionState.Stopping)
        }
        invalidateSession()
        capture?.stop()
        nativeSession?.stop()
        capture = null
        runtimeErrorDelivered = false
        state = RecognitionState.Ready
        if (listener != null) {
            dispatchState(listener, RecognitionState.Ready)
        }
        completeOperation(completion, null)
    }

    fun close(completion: (OperationResult) -> Unit) {
        if (state == RecognitionState.Closed) {
            completeOperation(completion, null)
            return
        }
        val listener = activeListener
        invalidateSession()
        ++controlEpoch
        capture?.stop()
        nativeSession?.stop()
        nativeSession?.close()
        capture = null
        activeListener = null
        state = RecognitionState.Closed
        if (listener != null) {
            dispatchState(listener, RecognitionState.Closed)
        }
        completeOperation(completion, null)
    }

    private fun onNativeEvent(generation: Long, event: NativeEvent) {
        when (event) {
            is NativeEvent.Recognized -> onRecognized(generation, event.result)
            is NativeEvent.Error -> onRuntimeError(generation, event.error)
        }
    }

    private fun onRecognized(generation: Long, result: RecognitionResult) {
        val listener = activeListener ?: return
        val queuedEpoch = controlEpoch
        ports.mainDispatcher.dispatch {
            if (state != RecognitionState.Listening || generation != sessionGeneration || queuedEpoch != controlEpoch) {
                return@dispatch
            }
            val nowMs = ports.monotonicClock.nowMs()
            val lastDeliveryMs = cooldownByTrigger[result.triggerId]
            if (lastDeliveryMs != null && nowMs - lastDeliveryMs < config.duplicateCooldownMs) {
                return@dispatch
            }
            cooldownByTrigger[result.triggerId] = nowMs
            listener.onRecognized(result)
        }
    }

    private fun onRuntimeError(generation: Long, error: RecognitionError) {
        val listener = activeListener ?: return
        if (state != RecognitionState.Listening || generation != sessionGeneration || runtimeErrorDelivered) {
            return
        }
        runtimeErrorDelivered = true
        invalidateSession()
        capture?.stop()
        nativeSession?.stop()
        capture = null
        state = RecognitionState.Ready
        val queuedEpoch = ++controlEpoch
        ports.mainDispatcher.dispatch {
            if (state != RecognitionState.Ready || queuedEpoch != controlEpoch) {
                return@dispatch
            }
            listener.onStateChanged(RecognitionState.Ready)
            listener.onError(error)
        }
    }

    private fun invalidateSession() {
        ++sessionGeneration
        cooldownByTrigger.clear()
    }

    private fun dispatchState(listener: RecognitionListener, state: RecognitionState) {
        val queuedEpoch = controlEpoch
        ports.mainDispatcher.dispatch {
            if (queuedEpoch == controlEpoch) {
                listener.onStateChanged(state)
            }
        }
    }

    private fun completePrepare(completion: (PrepareResult) -> Unit, error: RecognitionError?) {
        ports.mainDispatcher.dispatch {
            completion(if (error == null) PrepareResult.success() else PrepareResult.failure(error))
        }
    }

    private fun completeOperation(completion: (OperationResult) -> Unit, error: RecognitionError?) {
        ports.mainDispatcher.dispatch {
            completion(if (error == null) OperationResult.success() else OperationResult.failure(error))
        }
    }

    private fun invalidState(message: String): RecognitionError =
        RecognitionError(RecognitionErrorCode.InvalidState, message, recoverable = true)
}
