package com.localacr.android

import com.localacr.RecognitionConfig
import com.localacr.RecognitionError
import com.localacr.RecognitionErrorCode
import com.localacr.RecognitionResult
import com.localacr.internal.NativeEvent
import com.localacr.internal.NativeSessionPort
import com.localacr.internal.PcmBuffer
import java.nio.ByteBuffer

internal class NativeSessionJni(
    private val databasePath: String,
    private val config: RecognitionConfig,
    private val bridge: NativeBridge = JniNativeBridge,
) : NativeSessionPort {
    private var handle: Long = 0L
    private var generation: Long = 0L
    private var onEvent: ((NativeEvent) -> Unit)? = null

    override fun prepare(): RecognitionError? {
        val created = bridge.create(databasePath, 48_000)
        if (created.status != NativeBridgeStatus.Ok || created.value == null) {
            return created.status.toRecognitionError("native recognizer create failed")
        }
        handle = created.value
        return bridge.prepare(handle).toNullableError("native recognizer prepare failed")
    }

    override fun start(onEvent: (NativeEvent) -> Unit): RecognitionError? {
        if (handle == 0L) {
            return RecognitionError(RecognitionErrorCode.InvalidState, "native handle is not prepared", true)
        }
        this.onEvent = onEvent
        generation += 1
        return bridge.startSession(handle, generation).toNullableError("native recognizer start failed")
    }

    override fun pushPcm(pcm: PcmBuffer): RecognitionError? {
        val directBuffer = pcm.buffer as? ByteBuffer
        if (directBuffer == null || !directBuffer.isDirect) {
            return RecognitionError(
                RecognitionErrorCode.AudioEngineFailure,
                "Android native push requires a direct ByteBuffer",
                recoverable = true,
            )
        }
        val status = bridge.pushPcm(
            handle = handle,
            generation = generation,
            directBuffer = directBuffer,
            frames = pcm.frames,
            channels = pcm.channels,
            sampleRate = pcm.sampleRate,
            firstSourceFrame = pcm.firstSourceFrame,
            format = pcm.format,
        )
        if (status != NativeBridgeStatus.Ok) {
            val error = status.toRecognitionError("native recognizer push failed")
            onEvent?.invoke(NativeEvent.Error(error))
            return error
        }
        drainEvents()
        return null
    }

    override fun stop() {
        if (handle != 0L) {
            bridge.stopSession(handle, generation)
        }
    }

    override fun close() {
        if (handle != 0L) {
            bridge.destroy(handle)
            handle = 0L
        }
    }

    /** Delivers an error raised outside the native engine (e.g. a capture read failure) as a session event. */
    fun reportCaptureError(error: RecognitionError) {
        onEvent?.invoke(NativeEvent.Error(error))
    }

    fun pollOnceForTest() {
        pollOnce()
    }

    private fun drainEvents() {
        repeat(MaxEventsPerDrain) {
            if (!pollOnce()) {
                return
            }
        }
    }

    private fun pollOnce(): Boolean {
        val event = bridge.pollEvent(handle) ?: return false
        val listener = onEvent ?: return true
        when (event) {
            is NativeBridgeEvent.Recognition ->
                listener(
                    NativeEvent.Recognized(
                        RecognitionResult(
                            triggerId = event.triggerId,
                            displayName = event.triggerId,
                            confidence = event.confidence,
                            matchedPositionMs = event.matchedPositionMs,
                            resultAgeMs = event.resultAgeMs,
                            metadataJson = "{}",
                        ),
                    ),
                )
            is NativeBridgeEvent.SessionError ->
                listener(NativeEvent.Error(event.status.toRecognitionError("native session error")))
        }
        return true
    }

    private companion object {
        const val MaxEventsPerDrain = 32
    }
}

internal interface NativeBridge {
    fun create(databasePath: String, sampleRate: Int): NativeBridgeResult<Long>
    fun prepare(handle: Long): NativeBridgeStatus
    fun startSession(handle: Long, generation: Long): NativeBridgeStatus
    fun pushPcm(
        handle: Long,
        generation: Long,
        directBuffer: ByteBuffer,
        frames: Int,
        channels: Int,
        sampleRate: Int,
        firstSourceFrame: Long,
        format: PcmBuffer.Format,
    ): NativeBridgeStatus
    fun pollEvent(handle: Long): NativeBridgeEvent?
    fun stopSession(handle: Long, generation: Long): NativeBridgeStatus
    fun destroy(handle: Long)
}

internal data class NativeBridgeResult<T>(
    val status: NativeBridgeStatus,
    val value: T?,
) {
    companion object {
        fun <T> ok(value: T): NativeBridgeResult<T> = NativeBridgeResult(NativeBridgeStatus.Ok, value)
        fun <T> failure(status: NativeBridgeStatus): NativeBridgeResult<T> = NativeBridgeResult(status, null)
    }
}

internal enum class NativeBridgeStatus {
    Ok,
    InvalidArgument,
    ResourceLimitExceeded,
    AudioDiscontinuity,
    InvalidState,
    NativeEngineFailure,
    NoEvent,
}

internal sealed class NativeBridgeEvent {
    data class Recognition(
        val triggerId: String,
        val confidence: Float,
        val matchedPositionMs: Long,
        val resultAgeMs: Long,
    ) : NativeBridgeEvent()

    data class SessionError(val status: NativeBridgeStatus) : NativeBridgeEvent()
}

internal object JniNativeBridge : NativeBridge {
    private val loaded: Boolean

    init {
        loaded = runCatching { System.loadLibrary("local_acr_jni") }.isSuccess
    }

    override fun create(databasePath: String, sampleRate: Int): NativeBridgeResult<Long> {
        if (!loaded) {
            return NativeBridgeResult.failure(NativeBridgeStatus.NativeEngineFailure)
        }
        val handle = nativeCreate(databasePath, sampleRate)
        return if (handle == 0L) {
            NativeBridgeResult.failure(NativeBridgeStatus.NativeEngineFailure)
        } else {
            NativeBridgeResult.ok(handle)
        }
    }

    override fun prepare(handle: Long): NativeBridgeStatus = nativePrepare(handle).toNativeBridgeStatus()
    override fun startSession(handle: Long, generation: Long): NativeBridgeStatus =
        nativeStartSession(handle, generation).toNativeBridgeStatus()

    override fun pushPcm(
        handle: Long,
        generation: Long,
        directBuffer: ByteBuffer,
        frames: Int,
        channels: Int,
        sampleRate: Int,
        firstSourceFrame: Long,
        format: PcmBuffer.Format,
    ): NativeBridgeStatus =
        nativePushPcm(
            handle,
            generation,
            directBuffer,
            frames,
            channels,
            sampleRate,
            firstSourceFrame,
            format.ordinal + 1,
        ).toNativeBridgeStatus()

    override fun pollEvent(handle: Long): NativeBridgeEvent? {
        val slots = LongArray(PollSlotCount)
        val triggerId = nativePollEvent(handle, slots)
        return decodePolledEvent(slots, triggerId)
    }
    override fun stopSession(handle: Long, generation: Long): NativeBridgeStatus =
        nativeStopSession(handle, generation).toNativeBridgeStatus()

    override fun destroy(handle: Long) = nativeDestroy(handle)

    private external fun nativeCreate(databasePath: String, sampleRate: Int): Long
    private external fun nativePrepare(handle: Long): Int
    private external fun nativeStartSession(handle: Long, generation: Long): Int
    private external fun nativePushPcm(
        handle: Long,
        generation: Long,
        directBuffer: ByteBuffer,
        frames: Int,
        channels: Int,
        sampleRate: Int,
        firstSourceFrame: Long,
        format: Int,
    ): Int
    private external fun nativePollEvent(handle: Long, slots: LongArray): String?
    private external fun nativeStopSession(handle: Long, generation: Long): Int
    private external fun nativeDestroy(handle: Long)
}

internal const val PollSlotCount = 6
private const val AnalysisHopFrames = 128L
private const val AnalysisSampleRate = 11_025L

/** Decodes the slot array filled by nativePollEvent: status, type, error, cue frame, confidence x1000, aligned count. */
internal fun decodePolledEvent(slots: LongArray, triggerId: String?): NativeBridgeEvent? {
    val status = slots[0].toInt().toNativeBridgeStatus()
    if (status == NativeBridgeStatus.NoEvent) {
        return null
    }
    if (status != NativeBridgeStatus.Ok) {
        return NativeBridgeEvent.SessionError(status)
    }
    if (slots[1] == 1L && triggerId != null) {
        return NativeBridgeEvent.Recognition(
            triggerId = triggerId,
            confidence = slots[4] / 1000f,
            matchedPositionMs = slots[3] * AnalysisHopFrames * 1000L / AnalysisSampleRate,
            resultAgeMs = 0L,
        )
    }
    return NativeBridgeEvent.SessionError(
        when (slots[2]) {
            1L -> NativeBridgeStatus.ResourceLimitExceeded
            2L -> NativeBridgeStatus.AudioDiscontinuity
            else -> NativeBridgeStatus.NativeEngineFailure
        },
    )
}

private fun Int.toNativeBridgeStatus(): NativeBridgeStatus =
    when (this) {
        0 -> NativeBridgeStatus.Ok
        1 -> NativeBridgeStatus.InvalidArgument
        2 -> NativeBridgeStatus.ResourceLimitExceeded
        3 -> NativeBridgeStatus.AudioDiscontinuity
        4 -> NativeBridgeStatus.InvalidState
        7 -> NativeBridgeStatus.NoEvent
        else -> NativeBridgeStatus.NativeEngineFailure
    }

private fun NativeBridgeStatus.toNullableError(message: String): RecognitionError? =
    if (this == NativeBridgeStatus.Ok || this == NativeBridgeStatus.NoEvent) null else toRecognitionError(message)

private fun NativeBridgeStatus.toRecognitionError(message: String): RecognitionError =
    RecognitionError(
        code = when (this) {
            NativeBridgeStatus.Ok,
            NativeBridgeStatus.NoEvent -> RecognitionErrorCode.NativeEngineFailure
            NativeBridgeStatus.InvalidArgument -> RecognitionErrorCode.DatabaseInvalid
            NativeBridgeStatus.ResourceLimitExceeded -> RecognitionErrorCode.ResourceLimitExceeded
            NativeBridgeStatus.AudioDiscontinuity -> RecognitionErrorCode.AudioDiscontinuity
            NativeBridgeStatus.InvalidState -> RecognitionErrorCode.InvalidState
            NativeBridgeStatus.NativeEngineFailure -> RecognitionErrorCode.NativeEngineFailure
        },
        message = message,
        recoverable = this == NativeBridgeStatus.AudioDiscontinuity || this == NativeBridgeStatus.NoEvent,
    )
