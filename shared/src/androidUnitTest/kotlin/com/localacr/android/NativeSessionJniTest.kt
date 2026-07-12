package com.localacr.android

import com.localacr.RecognitionConfig
import com.localacr.RecognitionErrorCode
import com.localacr.internal.NativeEvent
import com.localacr.internal.PcmBuffer
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertSame
import kotlin.test.assertTrue

class NativeSessionJniTest {
    @Test
    fun pushPcmRequiresDirectBufferAndPreservesOneBulkBuffer() {
        val bridge = RecordingNativeBridge()
        val session = NativeSessionJni("db.lacrdb", RecognitionConfig(), bridge)
        assertEquals(null, session.prepare())
        assertEquals(null, session.start { })

        val direct = ByteBuffer.allocateDirect(256).order(ByteOrder.nativeOrder())
        val result = session.pushPcm(
            PcmBuffer(
                buffer = direct,
                frames = 64,
                channels = 1,
                sampleRate = 48_000,
                firstSourceFrame = 128,
                format = PcmBuffer.Format.S16Interleaved,
            ),
        )

        assertEquals(null, result)
        assertSame(direct, bridge.lastPushedBuffer)
        assertEquals(64, bridge.lastFrames)
        assertEquals(128, bridge.lastFirstSourceFrame)

        val heapResult = session.pushPcm(
            PcmBuffer(
                buffer = ByteBuffer.allocate(16),
                frames = 8,
                channels = 1,
                sampleRate = 48_000,
                firstSourceFrame = 0,
                format = PcmBuffer.Format.S16Interleaved,
            ),
        )
        assertEquals(RecognitionErrorCode.AudioEngineFailure, heapResult?.code)
    }

    @Test
    fun pollMapsNativeRecognitionAndErrorEvents() {
        val bridge = RecordingNativeBridge()
        val session = NativeSessionJni("db.lacrdb", RecognitionConfig(), bridge)
        val events = mutableListOf<NativeEvent>()
        session.prepare()
        session.start { events += it }

        bridge.nextEvent = NativeBridgeEvent.Recognition(
            triggerId = "promo",
            confidence = 0.75f,
            matchedPositionMs = 1_250,
            resultAgeMs = 30,
        )
        session.pollOnceForTest()
        assertTrue(events.single() is NativeEvent.Recognized)

        events.clear()
        bridge.nextEvent = NativeBridgeEvent.SessionError(NativeBridgeStatus.AudioDiscontinuity)
        session.pollOnceForTest()
        assertTrue(events.single() is NativeEvent.Error)
        assertEquals(RecognitionErrorCode.AudioDiscontinuity, (events.single() as NativeEvent.Error).error.code)
    }
}

private class RecordingNativeBridge : NativeBridge {
    var lastPushedBuffer: ByteBuffer? = null
    var lastFrames: Int = 0
    var lastFirstSourceFrame: Long = 0
    var nextEvent: NativeBridgeEvent? = null

    override fun create(databasePath: String, sampleRate: Int): NativeBridgeResult<Long> =
        NativeBridgeResult.ok(42L)

    override fun prepare(handle: Long): NativeBridgeStatus = NativeBridgeStatus.Ok
    override fun startSession(handle: Long, generation: Long): NativeBridgeStatus = NativeBridgeStatus.Ok
    override fun stopSession(handle: Long, generation: Long): NativeBridgeStatus = NativeBridgeStatus.Ok
    override fun destroy(handle: Long) = Unit

    override fun pushPcm(
        handle: Long,
        generation: Long,
        directBuffer: ByteBuffer,
        frames: Int,
        channels: Int,
        sampleRate: Int,
        firstSourceFrame: Long,
        format: PcmBuffer.Format,
    ): NativeBridgeStatus {
        lastPushedBuffer = directBuffer
        lastFrames = frames
        lastFirstSourceFrame = firstSourceFrame
        return NativeBridgeStatus.Ok
    }

    override fun pollEvent(handle: Long): NativeBridgeEvent? {
        val event = nextEvent
        nextEvent = null
        return event
    }
}
