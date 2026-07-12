package com.localacr.ios

import com.localacr.RecognitionConfig
import com.localacr.RecognitionError
import com.localacr.RecognitionErrorCode
import com.localacr.RecognitionResult
import com.localacr.internal.CapturePort
import com.localacr.internal.MainDispatcher
import com.localacr.internal.MonotonicClock
import com.localacr.internal.NativeEvent
import com.localacr.internal.NativeSessionPort
import com.localacr.internal.PcmBuffer
import com.localacr.internal.PlatformPorts
import com.localacr.iosbridge.LACR_ABI_VERSION
import com.localacr.iosbridge.LACR_ERROR_AUDIO_DISCONTINUITY
import com.localacr.iosbridge.LACR_ERROR_NATIVE_ENGINE_FAILURE
import com.localacr.iosbridge.LACR_ERROR_QUERY_DENSITY_EXCEEDED
import com.localacr.iosbridge.LACR_EVENT_RECOGNITION
import com.localacr.iosbridge.LACR_EVENT_SESSION_ERROR
import com.localacr.iosbridge.LACR_IOS_CAPTURE_AUDIO_ENGINE_FAILURE
import com.localacr.iosbridge.LACR_IOS_CAPTURE_INVALID_ARGUMENT
import com.localacr.iosbridge.LACR_IOS_CAPTURE_MICROPHONE_PERMISSION_DENIED
import com.localacr.iosbridge.LACR_IOS_CAPTURE_MICROPHONE_UNAVAILABLE
import com.localacr.iosbridge.LACR_IOS_CAPTURE_OK
import com.localacr.iosbridge.LACR_STATUS_AUDIO_DISCONTINUITY
import com.localacr.iosbridge.LACR_STATUS_BUFFER_TOO_SMALL
import com.localacr.iosbridge.LACR_STATUS_INVALID_ARGUMENT
import com.localacr.iosbridge.LACR_STATUS_INVALID_STATE
import com.localacr.iosbridge.LACR_STATUS_NATIVE_ENGINE_FAILURE
import com.localacr.iosbridge.LACR_STATUS_NO_EVENT
import com.localacr.iosbridge.LACR_STATUS_OK
import com.localacr.iosbridge.LACR_STATUS_RESOURCE_LIMIT_EXCEEDED
import com.localacr.iosbridge.lacr_config_t
import com.localacr.iosbridge.lacr_event_t
import com.localacr.iosbridge.lacr_ios_capture_create_handle
import com.localacr.iosbridge.lacr_ios_capture_destroy
import com.localacr.iosbridge.lacr_ios_capture_start
import com.localacr.iosbridge.lacr_ios_capture_stop
import com.localacr.iosbridge.lacr_ios_capture_t
import com.localacr.iosbridge.lacr_recognizer_destroy
import com.localacr.iosbridge.lacr_recognizer_poll_event
import com.localacr.iosbridge.lacr_ios_recognizer_create_prepared
import com.localacr.iosbridge.lacr_recognizer_start_session
import com.localacr.iosbridge.lacr_recognizer_stop_session
import com.localacr.iosbridge.lacr_recognizer_t
import kotlinx.cinterop.CPointer
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.UByteVar
import kotlinx.cinterop.ULongVar
import kotlinx.cinterop.alloc
import kotlinx.cinterop.allocArray
import kotlinx.cinterop.convert
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import kotlinx.cinterop.readBytes
import platform.Foundation.NSProcessInfo
import platform.darwin.dispatch_async
import platform.darwin.dispatch_get_main_queue
import kotlin.math.max

@OptIn(ExperimentalForeignApi::class)
class IosLocalAcrPorts : PlatformPorts {
    override val mainDispatcher: MainDispatcher =
        MainDispatcher { block -> dispatch_async(dispatch_get_main_queue()) { block() } }

    override val monotonicClock: MonotonicClock =
        object : MonotonicClock {
            override fun nowMs(): Long =
                (NSProcessInfo.processInfo.systemUptime * 1_000.0).toLong()
        }

    override fun openNativeSession(databasePath: String, config: RecognitionConfig): NativeSessionPort =
        IosNativeSession(databasePath)

    override fun openCapture(nativeSession: NativeSessionPort): CapturePort =
        IosCapture(nativeSession as? IosNativeSession)
}

@OptIn(ExperimentalForeignApi::class)
private class IosNativeSession(
    private val databasePath: String,
) : NativeSessionPort {
    private var recognizer: CPointer<lacr_recognizer_t>? = null
    private var generation: ULong = 0u
    private var listener: ((NativeEvent) -> Unit)? = null

    override fun prepare(): RecognitionError? = memScoped {
        val handle = lacr_ios_recognizer_create_prepared(databasePath, 48_000u, null)
        if (handle == null) {
            return@memScoped RecognitionError(
                RecognitionErrorCode.NativeEngineFailure,
                "native recognizer create failed",
                recoverable = false,
            )
        }
        recognizer = handle
        null
    }

    override fun start(onEvent: (NativeEvent) -> Unit): RecognitionError? {
        val handle = recognizer
            ?: return RecognitionError(RecognitionErrorCode.InvalidState, "native handle is not prepared", true)
        listener = onEvent
        generation += 1u
        return lacr_recognizer_start_session(handle, generation, null)
            .toNullableRecognitionError("native recognizer start failed")
    }

    override fun pushPcm(pcm: PcmBuffer): RecognitionError? =
        RecognitionError(
            RecognitionErrorCode.InvalidState,
            "iOS PCM is pushed by AVAudioEngine bridge, not Kotlin",
            recoverable = true,
        )

    override fun stop() {
        recognizer?.let { lacr_recognizer_stop_session(it, generation, null) }
    }

    override fun close() {
        recognizer?.let { lacr_recognizer_destroy(it) }
        recognizer = null
        listener = null
    }

    fun handle(): CPointer<lacr_recognizer_t>? = recognizer

    fun generation(): ULong = generation

    fun pollOnce() {
        val handle = recognizer ?: return
        val currentListener = listener ?: return
        memScoped {
            val event = alloc<lacr_event_t>()
            val payloadCapacity = 4096
            val payload = allocArray<UByteVar>(payloadCapacity)
            val required = alloc<ULongVar>()
            val status = lacr_recognizer_poll_event(handle, event.ptr, payload, payloadCapacity.convert(), required.ptr)
            when (status) {
                LACR_STATUS_OK -> currentListener(event.toNativeEvent(payload.readBytes(payloadCapacity)))
                LACR_STATUS_NO_EVENT -> Unit
                LACR_STATUS_BUFFER_TOO_SMALL -> currentListener(
                    NativeEvent.Error(
                        RecognitionError(
                            RecognitionErrorCode.ResourceLimitExceeded,
                            "native event payload exceeded iOS bridge buffer",
                            recoverable = true,
                        ),
                    ),
                )
                else -> currentListener(NativeEvent.Error(status.toRecognitionError("native poll failed")))
            }
        }
    }
}

@OptIn(ExperimentalForeignApi::class)
private class IosCapture(
    private val nativeSession: IosNativeSession?,
) : CapturePort {
    private var capture: CPointer<lacr_ios_capture_t>? = null

    override fun start(): RecognitionError? = memScoped {
        val session = nativeSession
            ?: return@memScoped RecognitionError(
                RecognitionErrorCode.InvalidState,
                "iOS capture requires an iOS native session",
                recoverable = true,
            )
        val handle = session.handle()
            ?: return@memScoped RecognitionError(
                RecognitionErrorCode.InvalidState,
                "iOS native session is not prepared",
                recoverable = true,
            )
        val handleCapture = lacr_ios_capture_create_handle(handle, session.generation(), 48_000u, null)
        if (handleCapture == null) {
            return@memScoped RecognitionError(
                RecognitionErrorCode.AudioEngineFailure,
                "iOS capture create failed",
                recoverable = true,
            )
        }
        capture = handleCapture
        val startStatus = lacr_ios_capture_start(handleCapture)
        if (startStatus != LACR_IOS_CAPTURE_OK) {
            lacr_ios_capture_destroy(handleCapture)
            capture = null
            return@memScoped startStatus.toCaptureError("iOS capture start failed")
        }
        null
    }

    override fun stop() {
        capture?.let {
            lacr_ios_capture_stop(it)
            lacr_ios_capture_destroy(it)
        }
        capture = null
    }
}

@OptIn(ExperimentalForeignApi::class)
private fun UInt.toNullableRecognitionError(message: String): RecognitionError? =
    if (this == LACR_STATUS_OK || this == LACR_STATUS_NO_EVENT) null else toRecognitionError(message)

@OptIn(ExperimentalForeignApi::class)
private fun UInt.toRecognitionError(message: String): RecognitionError =
    RecognitionError(
        code = when (this) {
            LACR_STATUS_INVALID_ARGUMENT -> RecognitionErrorCode.DatabaseInvalid
            LACR_STATUS_RESOURCE_LIMIT_EXCEEDED -> RecognitionErrorCode.ResourceLimitExceeded
            LACR_STATUS_AUDIO_DISCONTINUITY -> RecognitionErrorCode.AudioDiscontinuity
            LACR_STATUS_INVALID_STATE -> RecognitionErrorCode.InvalidState
            LACR_STATUS_NATIVE_ENGINE_FAILURE -> RecognitionErrorCode.NativeEngineFailure
            else -> RecognitionErrorCode.NativeEngineFailure
        },
        message = message,
        recoverable = this == LACR_STATUS_AUDIO_DISCONTINUITY || this == LACR_STATUS_NO_EVENT,
    )

@OptIn(ExperimentalForeignApi::class)
private fun UInt.toCaptureError(message: String): RecognitionError =
    RecognitionError(
        code = when (this) {
            LACR_IOS_CAPTURE_INVALID_ARGUMENT -> RecognitionErrorCode.InvalidState
            LACR_IOS_CAPTURE_MICROPHONE_PERMISSION_DENIED -> RecognitionErrorCode.MicrophonePermissionDenied
            LACR_IOS_CAPTURE_MICROPHONE_UNAVAILABLE -> RecognitionErrorCode.MicrophoneUnavailable
            LACR_IOS_CAPTURE_AUDIO_ENGINE_FAILURE -> RecognitionErrorCode.AudioEngineFailure
            else -> RecognitionErrorCode.AudioEngineFailure
        },
        message = message,
        recoverable = this != LACR_IOS_CAPTURE_INVALID_ARGUMENT,
    )

@OptIn(ExperimentalForeignApi::class)
private fun lacr_event_t.toNativeEvent(payload: ByteArray): NativeEvent =
    when (type) {
        LACR_EVENT_RECOGNITION -> {
            val start = trigger_id_offset.toInt()
            val length = trigger_id_length.toInt()
            val end = max(start, start + length).coerceAtMost(payload.size)
            val triggerId = payload.copyOfRange(start.coerceAtLeast(0), end).decodeToString()
            NativeEvent.Recognized(
                RecognitionResult(
                    triggerId = triggerId,
                    displayName = triggerId,
                    confidence = confidence,
                    matchedPositionMs = (matched_cue_time_frame.toLong() * 1_000L) / 11_025L,
                    resultAgeMs = 0,
                    metadataJson = "{}",
                ),
            )
        }
        LACR_EVENT_SESSION_ERROR ->
            NativeEvent.Error(error.toErrorCode().let { code ->
                RecognitionError(code, "native iOS session error", recoverable = code != RecognitionErrorCode.NativeEngineFailure)
            })
        else ->
            NativeEvent.Error(
                RecognitionError(
                    RecognitionErrorCode.NativeEngineFailure,
                    "unknown native iOS event",
                    recoverable = true,
                ),
            )
    }

@OptIn(ExperimentalForeignApi::class)
private fun UInt.toErrorCode(): RecognitionErrorCode =
    when (this) {
        LACR_ERROR_QUERY_DENSITY_EXCEEDED -> RecognitionErrorCode.QueryDensityExceeded
        LACR_ERROR_AUDIO_DISCONTINUITY -> RecognitionErrorCode.AudioDiscontinuity
        LACR_ERROR_NATIVE_ENGINE_FAILURE -> RecognitionErrorCode.NativeEngineFailure
        else -> RecognitionErrorCode.NativeEngineFailure
    }
