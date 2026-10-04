package com.localacr.android

import android.content.Context
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import com.localacr.RecognitionConfig
import com.localacr.internal.CapturePort
import com.localacr.internal.MainDispatcher
import com.localacr.internal.MonotonicClock
import com.localacr.internal.NativeSessionPort
import com.localacr.internal.PlatformPorts

class AndroidLocalAcrPorts(private val context: Context? = null) : PlatformPorts {
    override val mainDispatcher: MainDispatcher =
        MainDispatcher { block -> Handler(Looper.getMainLooper()).post(block) }

    override val monotonicClock: MonotonicClock =
        object : MonotonicClock {
            override fun nowMs(): Long = SystemClock.elapsedRealtime()
        }

    override fun openNativeSession(databasePath: String, config: RecognitionConfig): NativeSessionPort =
        NativeSessionJni(databasePath, config)

    override fun openCapture(nativeSession: NativeSessionPort): CapturePort =
        AudioRecordCapture(
            AndroidAudioSource(
                mediaRecorderSource = AndroidAudioRouting.preferredAudioSource(isUnprocessedSupported()),
            ),
            nativeSession,
            onCaptureError = { error -> (nativeSession as? NativeSessionJni)?.reportCaptureError(error) },
        )

    private fun isUnprocessedSupported(): Boolean {
        val manager = context?.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return false
        return manager.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED) == "true"
    }
}
