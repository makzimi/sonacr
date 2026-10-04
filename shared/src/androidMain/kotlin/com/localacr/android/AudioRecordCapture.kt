package com.localacr.android

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import com.localacr.RecognitionError
import com.localacr.RecognitionErrorCode
import com.localacr.internal.CapturePort
import com.localacr.internal.NativeSessionPort
import com.localacr.internal.PcmBuffer
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

internal class AudioRecordCapture(
    private val source: AudioSource,
    private val nativeSession: NativeSessionPort,
    private val onCaptureError: (RecognitionError) -> Unit = {},
) : CapturePort {
    private val running = AtomicBoolean(false)
    private var worker: Thread? = null
    var terminalErrorForTest: RecognitionError? = null
        private set

    override fun start(): RecognitionError? {
        if (!source.start()) {
            return RecognitionError(
                RecognitionErrorCode.MicrophoneUnavailable,
                "AudioRecord could not start",
                recoverable = true,
            )
        }
        running.set(true)
        val directBuffer = ByteBuffer
            .allocateDirect(source.capacityFrames * source.bytesPerFrame)
            .order(ByteOrder.nativeOrder())
        worker = thread(name = "LocalAcrAudioRecordCapture") {
            var firstSourceFrame = 0L
            while (running.get()) {
                val frames = source.read(directBuffer)
                if (frames == 0) {
                    running.set(false)
                    break
                }
                if (frames < 0) {
                    val readError = RecognitionError(
                        RecognitionErrorCode.AudioEngineFailure,
                        "AudioRecord read failed",
                        recoverable = true,
                    )
                    terminalErrorForTest = readError
                    if (running.get()) {
                        onCaptureError(readError)
                    }
                    running.set(false)
                    break
                }
                directBuffer.limit(frames * source.bytesPerFrame)
                directBuffer.position(0)
                val pushError = nativeSession.pushPcm(
                    PcmBuffer(
                        buffer = directBuffer,
                        frames = frames,
                        channels = source.channels,
                        sampleRate = source.sampleRate,
                        firstSourceFrame = firstSourceFrame,
                        format = PcmBuffer.Format.S16Interleaved,
                    ),
                )
                if (pushError != null) {
                    terminalErrorForTest = pushError
                    running.set(false)
                    break
                }
                firstSourceFrame += frames.toLong()
            }
            source.stop()
        }
        return null
    }

    override fun stop() {
        running.set(false)
        source.stop()
        val current = worker
        if (current != null && current !== Thread.currentThread()) {
            current.join(1_000)
        }
        worker = null
    }

    fun joinForTest() {
        worker?.join(1_000)
    }
}

internal interface AudioSource {
    val sampleRate: Int
    val channels: Int
    val bytesPerFrame: Int
    val capacityFrames: Int
    fun start(): Boolean
    fun read(buffer: ByteBuffer): Int
    fun stop()
}

internal class AndroidAudioSource(
    override val sampleRate: Int = 48_000,
    override val channels: Int = 1,
    override val capacityFrames: Int = 4_096,
    private val mediaRecorderSource: Int = MediaRecorder.AudioSource.MIC,
) : AudioSource {
    override val bytesPerFrame: Int = 2 * channels
    private val record: AudioRecord

    init {
        val minBytes = AudioRecord.getMinBufferSize(
            sampleRate,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        val bufferBytes = maxOf(minBytes, capacityFrames * bytesPerFrame)
        record = AudioRecord(
            mediaRecorderSource,
            sampleRate,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
            bufferBytes,
        )
    }

    override fun start(): Boolean =
        runCatching {
            record.startRecording()
            record.recordingState == AudioRecord.RECORDSTATE_RECORDING
        }.getOrDefault(false)

    override fun read(buffer: ByteBuffer): Int {
        buffer.clear()
        val bytes = record.read(buffer, capacityFrames * bytesPerFrame, AudioRecord.READ_BLOCKING)
        if (bytes < 0) {
            return -1
        }
        return bytes / bytesPerFrame
    }

    override fun stop() {
        runCatching {
            if (record.recordingState == AudioRecord.RECORDSTATE_RECORDING) {
                record.stop()
            }
        }
        record.release()
    }
}

internal object AndroidAudioRouting {
    fun preferredAudioSource(unprocessedSupported: Boolean): Int =
        if (unprocessedSupported) {
            MediaRecorder.AudioSource.UNPROCESSED
        } else {
            MediaRecorder.AudioSource.MIC
        }
}
