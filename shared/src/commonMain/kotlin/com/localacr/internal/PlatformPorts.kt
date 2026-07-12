package com.localacr.internal

import com.localacr.RecognitionConfig
import com.localacr.RecognitionError
import com.localacr.RecognitionErrorCode
import com.localacr.RecognitionResult

fun interface MainDispatcher {
    fun dispatch(block: () -> Unit)
}

interface MonotonicClock {
    fun nowMs(): Long
}

interface NativeSessionPort {
    fun prepare(): RecognitionError?
    fun start(onEvent: (NativeEvent) -> Unit): RecognitionError?
    fun pushPcm(pcm: PcmBuffer): RecognitionError?
    fun stop()
    fun close()
}

interface CapturePort {
    fun start(): RecognitionError?
    fun stop()
}

interface PlatformPorts {
    val mainDispatcher: MainDispatcher
    val monotonicClock: MonotonicClock
    fun openNativeSession(databasePath: String, config: RecognitionConfig): NativeSessionPort
    fun openCapture(nativeSession: NativeSessionPort): CapturePort
}

data class PcmBuffer(
    val buffer: Any,
    val frames: Int,
    val channels: Int,
    val sampleRate: Int,
    val firstSourceFrame: Long,
    val format: Format,
) {
    enum class Format {
        S16Interleaved,
        F32Interleaved,
        F32Planar,
    }
}

sealed class NativeEvent {
    data class Recognized(val result: RecognitionResult) : NativeEvent()
    data class Error(val error: RecognitionError) : NativeEvent()
}

object UnavailablePlatformPorts : PlatformPorts {
    override val mainDispatcher: MainDispatcher = MainDispatcher { block -> block() }
    override val monotonicClock: MonotonicClock = object : MonotonicClock {
        override fun nowMs(): Long = 0L
    }

    override fun openNativeSession(databasePath: String, config: RecognitionConfig): NativeSessionPort =
        object : NativeSessionPort {
            override fun prepare(): RecognitionError =
                RecognitionError(
                    RecognitionErrorCode.NativeEngineFailure,
                    "Platform native session is not installed",
                    recoverable = false,
                )

            override fun start(onEvent: (NativeEvent) -> Unit): RecognitionError =
                RecognitionError(
                    RecognitionErrorCode.NativeEngineFailure,
                    "Platform native session is not installed",
                    recoverable = false,
                )

            override fun pushPcm(pcm: PcmBuffer): RecognitionError =
                RecognitionError(
                    RecognitionErrorCode.NativeEngineFailure,
                    "Platform native session is not installed",
                    recoverable = false,
                )

            override fun stop() = Unit
            override fun close() = Unit
        }

    override fun openCapture(nativeSession: NativeSessionPort): CapturePort =
        object : CapturePort {
            override fun start(): RecognitionError =
                RecognitionError(
                    RecognitionErrorCode.MicrophoneUnavailable,
                    "Platform capture is not installed",
                    recoverable = true,
                )

            override fun stop() = Unit
        }
}

internal expect fun defaultPlatformPorts(): PlatformPorts
