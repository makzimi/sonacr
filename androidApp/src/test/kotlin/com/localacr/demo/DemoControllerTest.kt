package com.localacr.demo

import com.localacr.RecognitionError
import com.localacr.RecognitionErrorCode
import com.localacr.RecognitionResult
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class DemoControllerTest {
    private val catalog = TrackCatalog(mapOf("track-a" to TrackInfo("Song A", "Artist A")))

    @Test
    fun waitsForPermissionBeforeListening() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)

        controller.onScreenVisible(permissionGranted = false)

        assertEquals(DemoListeningStatus.PermissionRequired, controller.state.status)
        assertFalse(sdk.started)
    }

    @Test
    fun preparesAndStartsWhenPermissionGranted() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)

        controller.onScreenVisible(permissionGranted = true)

        assertEquals(DemoListeningStatus.Listening, controller.state.status)
        assertTrue(sdk.prepared)
        assertTrue(sdk.started)
    }

    @Test
    fun recognitionShowsNowPlayingFromCatalog() {
        val states = mutableListOf<DemoState>()
        val controller = DemoController(FakeDemoSdk(), catalog, onStateChanged = { states += it })
        controller.onScreenVisible(permissionGranted = true)

        controller.onRecognized(result("track-a"))

        val nowPlaying = controller.state.nowPlaying
        assertEquals("Song A", nowPlaying?.title)
        assertEquals("Artist A", nowPlaying?.artist)
        assertEquals(83_000L, nowPlaying?.matchedPositionMs)
        assertEquals(nowPlaying, states.last().nowPlaying)
    }

    @Test
    fun unknownTriggerFallsBackToTriggerId() {
        val controller = DemoController(FakeDemoSdk(), catalog)
        controller.onScreenVisible(permissionGranted = true)

        controller.onRecognized(result("unlisted"))

        assertEquals("unlisted", controller.state.nowPlaying?.title)
    }

    @Test
    fun differentTrackReplacesNowPlaying() {
        val controller = DemoController(FakeDemoSdk(), catalog)
        controller.onScreenVisible(permissionGranted = true)

        controller.onRecognized(result("track-a"))
        controller.onRecognized(result("track-b"))

        assertEquals("track-b", controller.state.nowPlaying?.triggerId)
    }

    @Test
    fun screenDisappearStopsListening() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)
        controller.onScreenVisible(permissionGranted = true)

        controller.onScreenHidden()

        assertTrue(sdk.stopped)
        assertEquals(DemoListeningStatus.Idle, controller.state.status)
    }

    @Test
    fun runtimeErrorRestartsListeningAutomatically() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)
        controller.onScreenVisible(permissionGranted = true)

        controller.onError(error("transient"))

        assertEquals(2, sdk.startCount)
        assertEquals(DemoListeningStatus.Listening, controller.state.status)
        assertNull(controller.state.errorMessage)
    }

    @Test
    fun repeatedErrorsWithoutRecognitionAreDisplayed() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)
        controller.onScreenVisible(permissionGranted = true)

        repeat(4) { controller.onError(error("broken")) }

        assertEquals(4, sdk.startCount)
        assertEquals(DemoListeningStatus.Error, controller.state.status)
        assertEquals("broken", controller.state.errorMessage)
    }

    @Test
    fun asyncStartFailuresAfterSynchronousSuccessAreDisplayedAfterCap() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)
        controller.onScreenVisible(permissionGranted = true)

        repeat(4) { controller.onError(error("microphone busy")) }

        assertEquals(DemoListeningStatus.Error, controller.state.status)
        assertEquals("microphone busy", controller.state.errorMessage)
    }

    @Test
    fun asyncPrepareFailureIsDisplayedInsteadOfFakeListening() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)
        controller.onScreenVisible(permissionGranted = true)
        sdk.startFailure = "database invalid"

        controller.onError(error("database invalid"))

        assertEquals(DemoListeningStatus.Error, controller.state.status)
        assertEquals("database invalid", controller.state.errorMessage)
    }

    private fun result(triggerId: String) =
        RecognitionResult(
            triggerId = triggerId,
            displayName = triggerId,
            confidence = 0.9f,
            matchedPositionMs = 83_000,
            resultAgeMs = 0,
            metadataJson = "{}",
        )

    private fun error(message: String) =
        RecognitionError(RecognitionErrorCode.NativeEngineFailure, message, recoverable = true)
}

class TrackCatalogTest {
    @Test
    fun parsesTabSeparatedLinesAndSkipsMalformedOnes() {
        val catalog = TrackCatalog.parse("a\tSong A\tArtist A\nb\tSong B\n\nbroken-line\n")

        assertEquals(2, catalog.size)
        assertEquals(TrackInfo("Song A", "Artist A"), catalog.lookup("a"))
        assertEquals(TrackInfo("Song B", ""), catalog.lookup("b"))
    }
}

private class FakeDemoSdk : DemoRecognizer {
    var prepared = false
    var started = false
    var stopped = false
    var startCount = 0
    var startFailure: String? = null

    override fun prepare(): DemoOperationResult {
        prepared = true
        return DemoOperationResult.Success
    }

    override fun start(listener: DemoRecognitionListener): DemoOperationResult {
        started = true
        startCount += 1
        return if (startFailure != null) {
            DemoOperationResult.Failure(startFailure!!)
        } else {
            DemoOperationResult.Success
        }
    }

    override fun stop(): DemoOperationResult {
        stopped = true
        return DemoOperationResult.Success
    }
}
