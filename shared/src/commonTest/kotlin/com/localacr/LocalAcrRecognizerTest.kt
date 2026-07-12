package com.localacr

import com.localacr.internal.CapturePort
import com.localacr.internal.MainDispatcher
import com.localacr.internal.MonotonicClock
import com.localacr.internal.NativeEvent
import com.localacr.internal.NativeSessionPort
import com.localacr.internal.PlatformPorts
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class LocalAcrRecognizerTest {
    @Test
    fun factoryRejectsInvalidInputWithTypedErrors() {
        assertEquals(
            RecognitionErrorCode.InvalidDatabasePath,
            LocalAcrFactory.create("", ports = FakePorts()).error?.code,
        )
        assertEquals(
            RecognitionErrorCode.InvalidDatabasePath,
            LocalAcrFactory.create("bad\u0000path", ports = FakePorts()).error?.code,
        )
        assertEquals(
            RecognitionErrorCode.InvalidConfig,
            LocalAcrFactory.create("db.lacrdb", RecognitionConfig(-1), FakePorts()).error?.code,
        )
    }

    @Test
    fun prepareStartStopCloseDispatchOnMainQueue() {
        val ports = FakePorts()
        val recognizer = LocalAcrFactory.create("db.lacrdb", ports = ports).recognizer!!
        val completions = mutableListOf<Boolean>()

        recognizer.prepare { completions += it.isSuccess }
        assertEquals(emptyList(), completions)
        ports.main.drain()
        assertEquals(listOf(true), completions)
        assertEquals(RecognitionState.Ready, recognizer.state)

        val listener = RecordingListener()
        recognizer.start(listener) { completions += it.isSuccess }
        ports.main.drain()
        assertEquals(listOf(true, true), completions)
        assertEquals(RecognitionState.Listening, listener.states.last())
        assertTrue(ports.capture.started)

        recognizer.stop { completions += it.isSuccess }
        ports.main.drain()
        assertEquals(listOf(true, true, true), completions)
        assertEquals(RecognitionState.Ready, listener.states.last())
        assertFalse(ports.capture.started)

        recognizer.close { completions += it.isSuccess }
        ports.main.drain()
        assertEquals(listOf(true, true, true, true), completions)
        assertEquals(RecognitionState.Closed, listener.states.last())
        assertTrue(ports.native.closed)
    }

    @Test
    fun cooldownSuppressesSameTriggerButNotDifferentTriggers() {
        val ports = FakePorts()
        val recognizer = preparedAndListening(ports)
        val listener = ports.listener!!

        ports.emitRecognition("a", 100)
        ports.main.drain()
        ports.emitRecognition("a", 10_000)
        ports.emitRecognition("b", 11_000)
        ports.main.drain()

        assertEquals(listOf("a", "b"), listener.results.map { it.triggerId })
    }

    @Test
    fun cooldownResetsForNewSession() {
        val ports = FakePorts()
        val recognizer = preparedAndListening(ports)
        val listener = ports.listener!!

        ports.emitRecognition("a", 100)
        ports.main.drain()
        recognizer.stop {}
        ports.main.drain()
        recognizer.start(listener) {}
        ports.main.drain()
        ports.emitRecognition("a", 200)
        ports.main.drain()

        assertEquals(listOf("a", "a"), listener.results.map { it.triggerId })
    }

    @Test
    fun stopAndCloseSuppressQueuedRecognitionDeliveries() {
        val ports = FakePorts()
        val recognizer = preparedAndListening(ports)
        val listener = ports.listener!!

        ports.emitRecognition("a", 100)
        recognizer.stop {}
        ports.main.drain()
        assertEquals(emptyList(), listener.results)

        recognizer.start(listener) {}
        ports.main.drain()
        ports.emitRecognition("b", 200)
        recognizer.close {}
        ports.main.drain()
        assertEquals(emptyList(), listener.results)
        assertEquals(RecognitionState.Closed, listener.states.last())
    }

    @Test
    fun runtimeErrorReturnsReadyAndEmitsExactlyOnce() {
        val ports = FakePorts()
        preparedAndListening(ports)
        val listener = ports.listener!!

        ports.emitError(RecognitionError(RecognitionErrorCode.AudioInterrupted, "interrupted", recoverable = true))
        ports.main.drain()
        ports.emitError(RecognitionError(RecognitionErrorCode.AudioInterrupted, "interrupted", recoverable = true))
        ports.main.drain()

        assertEquals(1, listener.errors.size)
        assertEquals(RecognitionState.Ready, listener.states.last())
        assertFalse(ports.capture.started)
    }

    @Test
    fun invalidCallsReturnInvalidStateWithoutMutation() {
        val ports = FakePorts()
        val recognizer = LocalAcrFactory.create("db.lacrdb", ports = ports).recognizer!!
        var startResult: OperationResult? = null
        recognizer.start(RecordingListener()) { startResult = it }
        ports.main.drain()

        assertEquals(RecognitionErrorCode.InvalidState, startResult?.error?.code)
        assertEquals(RecognitionState.Created, recognizer.state)
    }

    private fun preparedAndListening(ports: FakePorts): LocalAcrRecognizer {
        val recognizer = LocalAcrFactory.create("db.lacrdb", ports = ports).recognizer!!
        val listener = RecordingListener()
        ports.listener = listener
        recognizer.prepare {}
        ports.main.drain()
        recognizer.start(listener) {}
        ports.main.drain()
        return recognizer
    }
}

private class RecordingListener : RecognitionListener {
    val states = mutableListOf<RecognitionState>()
    val results = mutableListOf<RecognitionResult>()
    val errors = mutableListOf<RecognitionError>()

    override fun onStateChanged(state: RecognitionState) {
        states += state
    }

    override fun onRecognized(result: RecognitionResult) {
        results += result
    }

    override fun onError(error: RecognitionError) {
        errors += error
    }
}

private class FakeMainDispatcher : MainDispatcher {
    private val tasks = ArrayDeque<() -> Unit>()

    override fun dispatch(block: () -> Unit) {
        tasks += block
    }

    fun drain() {
        while (tasks.isNotEmpty()) {
            tasks.removeFirst().invoke()
        }
    }
}

private class FakeNativeSession : NativeSessionPort {
    var prepared = false
    var listening = false
    var closed = false
    var onEvent: ((NativeEvent) -> Unit)? = null

    override fun prepare(): RecognitionError? {
        prepared = true
        return null
    }

    override fun start(onEvent: (NativeEvent) -> Unit): RecognitionError? {
        this.onEvent = onEvent
        listening = true
        return null
    }

    override fun stop() {
        listening = false
    }

    override fun close() {
        closed = true
        listening = false
    }
}

private class FakeCapture : CapturePort {
    var started = false

    override fun start(): RecognitionError? {
        started = true
        return null
    }

    override fun stop() {
        started = false
    }
}

private class FakeClock : MonotonicClock {
    var now = 0L

    override fun nowMs(): Long = now
}

private class FakePorts : PlatformPorts {
    val main = FakeMainDispatcher()
    val native = FakeNativeSession()
    val capture = FakeCapture()
    val clock = FakeClock()
    var listener: RecordingListener? = null

    override val mainDispatcher: MainDispatcher = main
    override val monotonicClock: MonotonicClock = clock
    override fun openNativeSession(databasePath: String, config: RecognitionConfig): NativeSessionPort = native
    override fun openCapture(): CapturePort = capture

    fun emitRecognition(triggerId: String, nowMs: Long) {
        clock.now = nowMs
        native.onEvent?.invoke(
            NativeEvent.Recognized(
                RecognitionResult(
                    triggerId = triggerId,
                    displayName = "Cue $triggerId",
                    confidence = 0.9f,
                    matchedPositionMs = 1_000,
                    resultAgeMs = 25,
                    metadataJson = "{}",
                ),
            ),
        )
    }

    fun emitError(error: RecognitionError) {
        native.onEvent?.invoke(NativeEvent.Error(error))
    }
}
