# Architecture

This document records the decisions that define Sonacr. They change rarely. A change to the fingerprint pipeline or the hash layout makes every existing `.lacrdb` file invalid, so it needs a new profile ID and rebuilt databases.

The numbers below come from the code as of 2026-10-04. When this document and the code disagree, the code is right and this document needs a fix.

## Components

| Path | Role |
|---|---|
| `native/core` | The only implementation of resampling, spectrum analysis, peak picking, fingerprinting, the database reader and writer, and matching. Exposes the C API in `native/core/include/local_acr/local_acr.h`. |
| `native/cli` | `local_acr_db`, a macOS and Linux tool that builds, inspects and verifies `.lacrdb` files. |
| `native/android` | JNI bridge from Kotlin to the C API, built as `liblocal_acr_jni.so` by Gradle through the NDK. |
| `native/ios` | Objective-C++ bridge that owns `AVAudioEngine` and calls the C API. |
| `shared` | Kotlin Multiplatform SDK. Public API, lifecycle state machine, duplicate cooldown, error mapping and microphone capture. It has no signal processing code. |
| `androidApp`, `iosApp` | Sample apps. |
| `tools/bench`, `tools/probe` | Desktop benchmark and a C client that streams PCM through the C API. |
| `third_party` | Vendored KISS FFT, SpeexDSP and SQLite with pinned digests in `third_party/dependencies.lock.json`. |

Dependencies point one way. The apps depend on the SDK, the SDK depends on the C API, and the C API depends on the core. The core never depends on the CLI, Kotlin, JNI, Swift or UI code.

## Rules that hold everywhere

- One engine. The CLI, Android and iOS all run the same C++ code, so a database built on a Mac matches the phone bit for bit.
- A narrow C API. Callers see an opaque handle, plain structs and status codes. C++ types and exceptions never cross the boundary. Any change to the header needs a new `LACR_ABI_VERSION`, which is currently 1.
- Deterministic decisions. The core compiles without fast-math and with floating-point contraction off. Peak decisions compare signed Q16.16 fixed-point values, so tiny floating-point differences between platforms cannot change which peaks are chosen. Golden files in `native/tests/golden` lock this down.
- Pinned dependencies. Third-party code is vendored and checked against recorded digests. SQLite symbols are renamed to `lacr_sqlite3_*` so the engine cannot clash with a system SQLite.
- Local only. Microphone audio stays in memory while the engine computes fingerprints. The SDK never saves, uploads or logs it, and the Android app asks only for `RECORD_AUDIO`.

## Fingerprint pipeline

The fingerprint profile is the pipeline below. Its ID is stored in every database.

1. The engine converts input PCM to mono by averaging channels, then resamples it to 11,025 Hz with SpeexDSP. The database builder asks FFmpeg to decode directly to mono 11,025 Hz instead.
2. It cuts the signal into 512-sample frames every 128 samples, about 86 frames per second, with a Hann window.
3. It takes the power spectrum with KISS FFT and keeps bins 1 to 255. It applies natural-log compression, a per-bin frequency weight and a temporal high-pass filter with pole 64225/65536 in Q16.16.
4. It picks peaks against an adaptive threshold. Each peak raises the threshold of nearby bins through a Gaussian mask. The threshold falls by `ln(0.934)` per frame, so a loud moment stops masking new peaks after a few seconds. At most 5 peaks per frame are kept, and a peak is confirmed only if no stronger nearby peak appears within the next 22 frames.
5. It pairs each confirmed peak with up to 3 later peaks that are 4 to 96 frames later and at most 32 bins away in frequency.
6. Each pair becomes a 24-bit hash: anchor bin in bits 0 to 7, target bin in bits 8 to 15, time gap in bits 16 to 23. The database stores the hash with the anchor's frame number.

## Recognition

- The recognizer keeps the landmarks from the last 4 seconds of audio, up to 512. When there are more, it keeps the newest 512.
- It evaluates for the first time after 2 seconds and then every 22 frames, about every 255 ms.
- Each evaluation looks up the query hashes in batches of 256 and stops after 65,536 hash matches. A lookup that hits that limit skips the evaluation and the session continues.
- For each track, every hash match votes for the time offset between the track and the microphone audio. Offsets go into buckets 2 frames wide, and a bucket's score counts the query landmarks in it and its two neighbours, each landmark once.
- A track is rejected as ambiguous when a second offset at least 3 buckets away scores at least 80% of its best offset.
- The matcher profile then accepts the best track only if all of these hold:
  - at least 8 aligned landmarks;
  - aligned landmarks are at least 1.5% of the query, which is always true at the 512-landmark cap, so this gate is effectively off;
  - at least 5 more aligned landmarks than the best other track;
  - at least 1.25 times the other track's score when that score is not zero;
  - the same track won the previous evaluation, with its offset within 1 bucket.
- The winning offset gives the position in the track. Confidence is reported for display only and does not affect acceptance.

## Database

A `.lacrdb` file is a SQLite database with three tables:

```sql
CREATE TABLE database_metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID;
CREATE TABLE triggers (
  trigger_id TEXT PRIMARY KEY,
  display_name TEXT NOT NULL,
  duration_ms INTEGER NOT NULL CHECK(duration_ms > 0),
  metadata_json TEXT NOT NULL
) WITHOUT ROWID;
CREATE TABLE fingerprints (
  trigger_id TEXT NOT NULL,
  hash INTEGER NOT NULL CHECK(hash BETWEEN 0 AND 16777215),
  time_frame INTEGER NOT NULL CHECK(time_frame >= 0),
  PRIMARY KEY(trigger_id, hash, time_frame),
  FOREIGN KEY(trigger_id) REFERENCES triggers(trigger_id)
) WITHOUT ROWID;
CREATE INDEX fingerprints_by_hash ON fingerprints(hash, trigger_id, time_frame);
```

The metadata table records the schema version, the fingerprint and matcher profile IDs, the database ID and version, the decoder version, a build report and a SHA-256 digest of the content.

The runtime opens the file read-only and refuses it unless the profiles match, SQLite's integrity and foreign-key checks pass and the digest matches. It also enforces these limits, and the builder must stay within them:

| Limit | Value |
|---|---|
| File size | 256 MiB |
| Tracks | 100 |
| Fingerprint rows | 1,000,000 |
| Rows for one track | 100,000, about 23 minutes of music |
| Rows sharing one hash | 128 |

## Database builder

`local_acr_db build <manifest.json> <output.lacrdb>` reads a JSON manifest. The manifest format is in the README.

- Track IDs match `[A-Za-z0-9][A-Za-z0-9._-]{0,127}`. A manifest has at most 100 tracks and 60 minutes of audio, and each file is at most 100 MiB.
- Audio paths must stay inside the manifest's folder. The builder runs FFmpeg with an argument list and never through a shell.
- Before writing, the builder plays every 2-second window of every track against the others through the runtime matcher. If any two tracks match each other, the build fails, because the app could confuse them.
- The output is written to a temporary file and renamed into place only after it verifies.

## Threading

`lacr_recognizer_push_pcm` does all the work on the caller's thread under a mutex: resampling, peaks, lookups and matching. Events wait in a queue of up to 32 until the caller polls.

- Android reads the microphone with `AudioRecord` on its own thread, 4,096 frames at 48 kHz at a time. After each push it drains the events and passes them to the SDK, which delivers them on the main thread. On a Pixel 7 a push takes about 31 ms for 85 ms of audio.
- iOS calls the C API from the `AVAudioEngine` input tap, which is a real-time audio thread. That is not acceptable for release. The roadmap moves this work to a worker thread fed by the lock-free queue in `native/core/src/session/spsc_pcm_queue.*`.

## SDK lifecycle

`LocalAcrRecognizer` moves through prepare, start, stop and close. Results and errors arrive on the main thread. The SDK suppresses repeated results for the same track for `duplicateCooldownMs`, 30 seconds by default, and a different track is reported immediately.
