package com.localacr.demo

import com.localacr.RecognitionError
import com.localacr.RecognitionErrorCode
import com.localacr.RecognitionResult
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class DemoControllerTest {
    @Test
    fun waitsForPermissionBeforeListening() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk)

        controller.onScreenVisible(permissionGranted = false)

        assertEquals(DemoListeningStatus.PermissionRequired, controller.state.status)
        assertFalse(sdk.started)
    }

    @Test
    fun preparesAndStartsWhenPermissionGranted() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk)

        controller.onScreenVisible(permissionGranted = true)

        assertEquals(DemoListeningStatus.Listening, controller.state.status)
        assertTrue(sdk.prepared)
        assertTrue(sdk.started)
    }

    @Test
    fun recognitionShowsPromotionFromMetadata() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk)
        controller.onScreenVisible(permissionGranted = true)

        sdk.listener!!.onRecognized(
            RecognitionResult(
                triggerId = "welcome",
                displayName = "Welcome cue",
                confidence = 0.92f,
                matchedPositionMs = 1_250,
                resultAgeMs = 35,
                metadataJson = "{\"title\":\"20% off\",\"body\":\"Show this today\",\"cta\":\"Claim\"}",
            ),
        )

        assertEquals("20% off", controller.state.promotion?.title)
        assertEquals("Show this today", controller.state.promotion?.body)
        assertEquals("Claim", controller.state.promotion?.cta)
        assertEquals("welcome", controller.state.lastRecognizedTriggerId)
    }

    @Test
    fun stateObserverReceivesRecognitionUpdates() {
        val sdk = FakeDemoSdk()
        val observed = mutableListOf<DemoState>()
        val controller = DemoController(sdk, onStateChanged = { observed += it })
        controller.onScreenVisible(permissionGranted = true)

        sdk.emit("same", "{\"title\":\"First\",\"body\":\"Body\",\"cta\":\"Open\"}")

        assertEquals("First", observed.last().promotion?.title)
    }

    @Test
    fun repeatedTriggerWithinUiCooldownDoesNotReplacePromotion() {
        val sdk = FakeDemoSdk()
        val clock = FakeClock()
        val controller = DemoController(sdk, clock)
        controller.onScreenVisible(permissionGranted = true)

        sdk.emit("same", "{\"title\":\"First\",\"body\":\"Body\",\"cta\":\"Open\"}")
        clock.nowMs = 1_000
        sdk.emit("same", "{\"title\":\"Second\",\"body\":\"Body\",\"cta\":\"Open\"}")
        sdk.emit("other", "{\"title\":\"Other\",\"body\":\"Body\",\"cta\":\"Open\"}")

        assertEquals("Other", controller.state.promotion?.title)
    }

    @Test
    fun localCtaAndDismissUpdateStateWithoutNetworkAction() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk)
        controller.onScreenVisible(permissionGranted = true)
        sdk.emit("same", "{\"title\":\"First\",\"body\":\"Body\",\"cta\":\"Open\"}")

        controller.onPromotionCta()
        assertEquals("Claimed locally", controller.state.localActionMessage)
        controller.onDismissPromotion()
        assertEquals(null, controller.state.promotion)
    }

    @Test
    fun screenDisappearStopsListening() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk)
        controller.onScreenVisible(permissionGranted = true)

        controller.onScreenHidden()

        assertTrue(sdk.stopped)
        assertEquals(DemoListeningStatus.Idle, controller.state.status)
    }

    @Test
    fun recognizerErrorIsDisplayed() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk)
        controller.onScreenVisible(permissionGranted = true)

        sdk.listener!!.onError(
            RecognitionError(RecognitionErrorCode.AudioInterrupted, "interrupted", recoverable = true),
        )

        assertEquals(DemoListeningStatus.Error, controller.state.status)
        assertEquals("interrupted", controller.state.errorMessage)
    }
}

private class FakeClock : DemoClock {
    var nowMs: Long = 0
    override fun nowMs(): Long = nowMs
}

private class FakeDemoSdk : DemoRecognizer {
    var listener: DemoRecognitionListener? = null
    var prepared = false
    var started = false
    var stopped = false

    override fun prepare(): DemoOperationResult {
        prepared = true
        return DemoOperationResult.Success
    }

    override fun start(listener: DemoRecognitionListener): DemoOperationResult {
        this.listener = listener
        started = true
        return DemoOperationResult.Success
    }

    override fun stop(): DemoOperationResult {
        stopped = true
        started = false
        return DemoOperationResult.Success
    }

    fun emit(triggerId: String, metadataJson: String) {
        listener!!.onRecognized(
            RecognitionResult(
                triggerId = triggerId,
                displayName = triggerId,
                confidence = 0.9f,
                matchedPositionMs = 1,
                resultAgeMs = 1,
                metadataJson = metadataJson,
            ),
        )
    }
}
