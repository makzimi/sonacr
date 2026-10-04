package com.localacr.android

import com.localacr.RecognitionConfig
import com.localacr.RecognitionErrorCode
import com.localacr.internal.NativeEvent
import com.localacr.internal.PcmBuffer
import java.nio.ByteBuffer
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class AudioRecordCaptureTest {
    @Test
    fun sourceSelectorPrefersUnprocessedOnlyWhenSupported() {
        assertEquals(
            android.media.MediaRecorder.AudioSource.UNPROCESSED,
            AndroidAudioRouting.preferredAudioSource(unprocessedSupported = true),
        )
        assertEquals(
            android.media.MediaRecorder.AudioSource.MIC,
            AndroidAudioRouting.preferredAudioSource(unprocessedSupported = false),
        )
    }

    @Test
    fun captureUsesOneDirectBufferAndConsecutiveSourceFrames() {
        val source = FakeAudioSource(listOf(32, 32, 0))
        val native = RecordingNativeSession()
        val capture = AudioRecordCapture(source, native)

        assertEquals(null, capture.start())
        capture.joinForTest()

        assertEquals(2, native.buffers.size)
        assertTrue(native.buffers.all { (it.buffer as ByteBuffer).isDirect })
        assertTrue(native.buffers.map { it.buffer as ByteBuffer }.distinct().size == 1)
        assertEquals(listOf(0L, 32L), native.buffers.map { it.firstSourceFrame })
        assertFalse(source.started)
    }

    @Test
    fun captureMapsReadFailureToAudioEngineFailure() {
        val source = FakeAudioSource(listOf(-1))
        val native = RecordingNativeSession()
        val capture = AudioRecordCapture(source, native)

        val error = capture.start()
        capture.joinForTest()

        assertEquals(null, error)
        assertEquals(RecognitionErrorCode.AudioEngineFailure, capture.terminalErrorForTest?.code)
        assertFalse(source.started)
    }

    @Test
    fun readFailureIsReportedToCaptureErrorCallbackExactlyOnce() {
        val source = FakeAudioSource(listOf(-1))
        val native = RecordingNativeSession()
        val reported = mutableListOf<com.localacr.RecognitionError>()
        val capture = AudioRecordCapture(source, native, onCaptureError = { reported += it })

        capture.start()
        capture.joinForTest()

        assertEquals(1, reported.size)
        assertEquals(RecognitionErrorCode.AudioEngineFailure, reported.single().code)
    }
}

private class FakeAudioSource(private val reads: List<Int>) : AudioSource {
    private var index = 0
    override val sampleRate: Int = 48_000
    override val channels: Int = 1
    override val bytesPerFrame: Int = 2
    override val capacityFrames: Int = 64
    var started = false

    override fun start(): Boolean {
        started = true
        return true
    }

    override fun read(buffer: ByteBuffer): Int {
        val frames = reads[index++]
        if (frames > 0) {
            buffer.clear()
            repeat(frames * bytesPerFrame) { buffer.put(0) }
        }
        return frames
    }

    override fun stop() {
        started = false
    }
}

private class RecordingNativeSession : com.localacr.internal.NativeSessionPort {
    val buffers = mutableListOf<PcmBuffer>()

    override fun prepare() = null
    override fun start(onEvent: (NativeEvent) -> Unit) = null
    override fun pushPcm(pcm: PcmBuffer) = null.also { buffers += pcm }
    override fun stop() = Unit
    override fun close() = Unit
}
