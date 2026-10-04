# Roadmap

This is the list of remaining work. Finished work lives in git history, and the decisions behind the current design are in [architecture.md](architecture.md). Measured results are in [qualification/device-matrix.md](qualification/device-matrix.md) and [qualification/conservative-v1-profile.md](qualification/conservative-v1-profile.md).

## Where things stand

- The Android sample recognizes songs on a Pixel 7. At 1 m from a MacBook speaker it named 10 of 15 clips, never named the wrong song, and gave no false matches in 10 minutes of unrelated audio.
- On a desktop the same 5 songs score 80 of 80 on 5-second clips, with and without added noise.
- iOS builds only for the simulator and does not deliver results to the UI.

## 1. Recognize more reliably over the air

The phone misses a third of the clips. Replaying the phone's own recordings on a desktop gives the same results, so the cause is the acoustic path, not the Android code. Only 3 to 7 exact fingerprint matches survive a room and a laptop speaker, against the 8 the matcher requires. Clean audio of the same clip gives 35 to 58.

Steps:

1. Record at least 30 phone captures, 5 tracks at 3 positions and 2 distances, 1 m and 3 m, and keep them in the git-ignored `local-tracks/ota/`. Add an `--ota-dir` option to `tools/bench/recognition_bench.py` that scores them.
2. Collect at least an hour of unrelated audio for the false-match test. It must come from `local-tracks/`, supplied by the project owner.
3. Try these one at a time, keeping a change only if the phone captures improve and the desktop benchmark and the false-match test stay clean:

| Idea | Change |
|---|---|
| Room noise crowds out the music's peaks | Normalize the spectrum per band before picking peaks, or allow more peaks per frame with a threshold based on the local average |
| A 1-frame timing shift breaks the exact hash | Store the time gap, and maybe the frequency bins, at coarser resolution |
| Keeping only the newest 512 landmarks shortens the 4-second window | Keep the strongest landmarks per time slice instead |
| Android's microphone processing hurts music | Capture with `VOICE_RECOGNITION` or `UNPROCESSED`. The SDK needs a `Context` to detect `UNPROCESSED` support |
| The evidence gate could be smarter | Replace the inactive 1.5% coverage gate with a test of how far the winner stands above the background |

4. Songs repeat sections, so the same clip can line up at two places in one track. Today that counts as ambiguous and blocks a match. A repeat inside one track should not block naming the track.

## 2. iOS

1. Add an `iosArm64` target, build `native/ios` for devices, package an XCFramework, and set up signing.
2. Deliver results. `IosLocalAcrPorts.pollOnce()` has no caller. Drain events after each push, as Android does.
3. Move the engine work off the real-time audio thread. The `AVAudioEngine` tap should only copy into the existing lock-free queue in `native/core/src/session/spsc_pcm_queue.*`, and a worker thread should do the rest.
4. Replace the stale `iosApp/Resources/venue-demo.lacrdb`. Build it with `tools/demo/build_demo_assets.py` and port the now-playing screen to SwiftUI.

## 3. Long recordings such as films

The current limits block this:

- One track can hold 100,000 fingerprints, about 23 minutes of music, and a manifest can hold 60 minutes of audio.
- The runtime refuses a database where one hash appears more than 128 times. The builder does not drop such hashes, so a large library can build fine and then fail to open on the phone. The builder should drop them and list them in its report.
- Dialogue and quiet scenes produce far fewer fingerprints than music.

## 4. Hardening

- Rename the profiles to `landmark-v2` and `conservative-v2`. The fingerprints and gates changed in October 2026, but databases still carry the old names, so an old database would load and match badly instead of being refused.
- Pass track names and metadata through the C API. Today the SDK fills `displayName` with the track ID and `metadataJson` with `{}`, and the demo reads names from `catalog.tsv` instead. `resultAgeMs` is always 0.
- `LocalAcrRecognizer` changes its state from the capture thread when a runtime error arrives. Move that handling to the main thread.
- The demo leaks native recognizers. It never calls `close()`, and granting the microphone permission the first time prepares twice.
- Build the Android native library in release mode and measure CPU use on the phone.
- Reset the pending event in `lacr_recognizer_start_session`, and limit track IDs to ASCII so `NewStringUTF` in the JNI bridge always receives valid input.
