# Local ACR MVP Design

Date: 2026-07-11

Status: Approved design

## 1. Purpose

Local ACR is an offline audio-content-recognition SDK and demonstration application for Android and iOS. It recognizes a small set of short audio cues heard through a device microphone and emits typed callbacks that native applications can turn into venue check-ins, promotions, or synchronized content.

The MVP demonstrates two original short cue clips. A command-line tool builds a local fingerprint database from WAV or MP3 inputs. The Android and iOS applications bundle that database, begin listening automatically on a dedicated foreground screen after microphone permission is granted, and show a native promotion when a cue is recognized.

The design prioritizes false-positive avoidance, deterministic behavior, testability, and consistent fingerprints across the database tool and both mobile platforms.

## 2. MVP requirements

### 2.1 Functional requirements

- Build a Kotlin Multiplatform project with:
  - a shared KMP library;
  - an Android application using Jetpack Compose;
  - an iOS application using SwiftUI;
  - a portable native C++ recognition engine;
  - a macOS/Linux command-line database builder.
- Accept a JSON manifest plus WAV or MP3 source files and produce one `.lacrdb` database.
- Bundle the database in each sample application. Runtime database download, replacement, and networking are excluded.
- Start microphone capture automatically when the recognition screen is visible and permission has been granted.
- Recognize moderate over-the-air playback within two to four seconds.
- Return a typed result containing:
  - stable trigger ID;
  - display name;
  - normalized confidence;
  - matched position inside the cue;
  - opaque metadata JSON.
- Allow native applications to decide how to react. The SDK must not open URLs, render ads, or interpret promotion actions.
- Suppress a repeated callback for the same trigger for a configurable cooldown, defaulting to 30 seconds. A different trigger may fire immediately. Cooldown state resets when a new listening session begins.

### 2.2 Platform and scale requirements

- Android minimum: API 26.
- iOS minimum: iOS 15.
- Database-builder hosts: macOS and Linux.
- MVP database profile: at most 10 total hours of source audio; the supplied demo contains only several short clips.
- Cue duration: 4 to 60 seconds. Five to fifteen seconds is recommended.
- Listening works only while the dedicated recognition screen is in the foreground.

### 2.3 Quality priorities

- Prefer no result over an incorrect result.
- Keep raw microphone audio in memory only. Never persist, upload, or log it.
- Use one fingerprint implementation for the CLI, Android, and iOS.
- Keep the real-time audio callback free of allocation, locks, FFT work, logging, and database access.
- Make all public asynchronous failures typed and recoverable where possible.

## 3. Explicitly out of scope

- Background or screen-locked listening.
- Server-side recognition or any network endpoint.
- Legacy `.runacr` reading or generation.
- Database download/update support.
- Audioneex codebooks or large-catalog posting-list indexes.
- An ad network, analytics SDK, or production promotion service.
- Arbitrary-hour streams, broadcast-station recognition, or acoustic data transmission.
- Windows support for the database builder.

## 4. Reference analysis and adopted ideas

The implementation is a new codebase. It uses the original RunACR landmark approach as its algorithmic foundation and independently implements selected ideas from the supplied references.

| Reference | Adopted ideas | Deliberate exclusions |
| --- | --- | --- |
| `Smaaash_RunACR` and `RunACR-Android-master` | 11,025 Hz canonical rate, landmark peak pairs, compact hashes, local time-offset voting, recognition callbacks | Python 2/MySQL pipeline, four duplicated client spectrogram shifts, license certificate, unindexed SQLite query, platform-specific DSP copies |
| `stream-audio-fingerprint-master` | Bounded incremental processing, delayed peak confirmation, decaying adaptive masks, frequency-dependent masking | Node/TypeScript implementation and source copying under MPL-2.0 |
| `audioRecognition-master` (Audioneex) | Fingerprinter/matcher/datastore separation, process/flush/reset lifecycle, bounded buffers, incremental matching, winner separation | Descriptor codebook, posting-list index, Tokyo Cabinet/Couchbase dependencies, source copying under MPL-2.0 |
| `seek-tune-main` | Structured trigger metadata, parameterized SQLite access, candidate-local offset bucketing | Recursive allocating FFT, naive downsampling, N+1 lookup, and a map model that loses repeated fingerprint addresses |
| `audio-recognizer-master` | Independent confirmation of offset-histogram matching | Coarse frequency bands and proof-of-concept architecture |
| `data-over-sound-master` | Thin mobile wrappers around a portable C++ core and a callback-oriented API | Proprietary acoustic-modem engine, binary reuse, or reverse engineering |
| `ACR-main` | Privacy emphasis and local-only product positioning | Network-capture and television-tracking tooling |

No MPL or proprietary engine source will be copied into Local ACR. Third-party production dependencies must have permissive terms and be pinned with license notices.

## 5. System architecture

### 5.1 Components

1. `native/core`
   - Portable C++ engine containing streaming resampling, FFT analysis, peak selection, landmark hashing, database access, matching, and confidence evaluation.
   - Exposes a narrow opaque C ABI. C++ types, standard-library types, ownership, and exceptions never cross the ABI.

2. `tools/local-acr-db`
   - Native macOS/Linux executable.
   - Validates the manifest and audio, invokes FFmpeg as an external decoder, feeds decoded PCM to `native/core`, and writes `.lacrdb` through the same pinned SQLite implementation used by the engine.

3. `shared`
   - Kotlin Multiplatform library containing the public API, lifecycle state machine, callback serialization, cooldown policy, typed error mapping, and platform abstractions.
   - Contains no FFT, spectrogram, peak, hash, or matching implementation.

4. `shared/androidMain`
   - Captures microphone PCM with `AudioRecord` and invokes the native C ABI through bulk JNI calls.

5. `shared/iosMain`
   - Captures microphone PCM with `AVAudioEngine` and invokes the same C ABI through Kotlin/Native C interop.

6. `androidApp`
   - Requests microphone permission, resolves/copies the bundled database to a filesystem path, manages screen lifecycle, collects SDK callbacks, parses the demo promotion metadata, and renders Compose UI.

7. `iosApp`
   - Requests microphone permission, resolves the bundled database path, manages view lifecycle, receives SDK callbacks, parses the demo promotion metadata, and renders SwiftUI.

### 5.2 Dependency direction

- Native UI depends on the shared public KMP API.
- KMP common code depends on small platform abstractions for audio capture, main-thread dispatch, and the native engine session.
- Platform actual implementations depend on platform audio APIs and the C ABI.
- The C++ core depends on internal interfaces for FFT, resampling, database access, clock-free audio processing, and diagnostics.
- The CLI depends on the C++ core; the core never depends on the CLI, KMP, JNI, Swift, or UI.

### 5.3 Native build products

- macOS/Linux: static core library plus `local-acr-db` executable.
- Android: `liblocal_acr.so` for supported ABIs, loaded by the Android KMP implementation.
- iOS: static library slices packaged for device and simulator and linked into the KMP framework/application.

The same public C header is consumed by JNI wrapper code and Kotlin/Native cinterop.

## 6. Audio and fingerprint pipeline

### 6.1 Input boundary

The native engine accepts interleaved PCM batches with explicit sample rate, channel count, sample format, and monotonic session sample position. Production mobile adapters provide mono input. The CLI uses FFmpeg to decode and mix input to mono while preserving and reporting the decoded sample rate. Canonical resampling remains inside the C++ core.

Android captures PCM16 at a supported native device rate. It prefers unprocessed input where supported and falls back to documented microphone input. iOS captures Float32 from an `AVAudioEngine` input tap using the hardware format and a measurement-oriented audio session.

### 6.2 Canonical signal

- Sample rate: 11,025 Hz.
- Channels: one.
- Internal sample type: normalized `float` in `[-1, 1]`.
- FFT window: 512 samples.
- Hop: 128 samples.
- Window: symmetric Hann.
- Spectrum: real-input power spectrum, positive-frequency bins only.

A pinned streaming resampler processes platform-native input. A pinned real-FFT implementation processes preallocated 512-sample frames. The initial dependency choices are SpeexDSP's resampler and KISS FFT; both remain hidden behind internal interfaces and can be replaced without changing the C ABI, database schema, or KMP API. Their exact commits and license notices are pinned by the implementation plan.

### 6.3 Peak selection profile `landmark-v1`

1. Apply the Hann window and compute a power spectrum.
2. Apply logarithmic magnitude compression with a bounded floor.
3. Apply frequency weighting proportional to `sqrt(bin + 16)` so low-frequency crowd and room energy does not dominate.
4. Apply a temporal high-pass response with pole `0.98` to reduce stationary spectral energy.
5. Maintain a forward adaptive threshold. A selected peak raises a Gaussian frequency mask whose standard deviation is `3 * sqrt(bin + 3)` bins. The mask decays by `0.997` per hop.
6. Keep at most five local maxima in each frame.
7. Delay final peak emission by 22 hops, approximately 255 ms. During that period, discard a provisional peak when a stronger later peak supersedes it within the adaptive mask.

All state is bounded. End-of-input flush finalizes peaks that have enough evidence and deterministically discards incomplete candidates.

### 6.4 Landmarks and hashes

For every confirmed anchor peak:

- consider target peaks 4 through 96 hops later;
- require an absolute frequency-bin delta no greater than 32;
- emit at most three target pairs, ordered by time and then frequency;
- deduplicate identical `(hash, time_frame)` pairs.

The stable 32-bit `landmark-v1` hash packs:

- anchor bin: 8 bits;
- target bin: 8 bits;
- delta time: 8 bits;
- upper 8 bits reserved and set to zero.

The database stores the algorithm profile separately. A runtime rejects a database whose profile is not exactly supported.

## 7. Streaming recognition and matching

### 7.1 Query schedule

- Maintain at most four seconds of query landmarks.
- Begin evaluation once two seconds of audio evidence has been processed.
- Re-evaluate every 22 hops, approximately 255 ms.
- Remove expired samples, frames, peaks, and landmarks incrementally.

### 7.2 Candidate lookup

The matcher batches unique query hashes and uses the `fingerprints(hash, trigger_id, time_frame)` index. It produces candidate hits containing query time, database time, trigger ID, and hash. A query landmark contributes at most one vote to a given trigger/offset bucket, preventing duplicate database rows or common hashes from inflating confidence.

### 7.3 Temporal alignment

For each trigger independently:

1. Compute `database_time - query_session_time` for each hit.
2. Quantize offsets into two-hop buckets, approximately 23 ms.
3. Count the winning bucket together with its immediate neighboring buckets.
4. Track the runner-up trigger and the winning offset from the previous evaluation.

Candidate state is cleared or aged independently; histogram state is never shared between triggers.

### 7.4 Conservative acceptance gates

A trigger is emitted only when all conditions hold:

- at least 12 unique aligned landmarks;
- aligned landmarks are at least 12% of the current unique query landmarks;
- the winner has at least five more aligned landmarks than the runner-up;
- if the runner-up has a nonzero score, the winner/runner-up ratio is at least `1.25`;
- the same trigger wins two consecutive evaluations;
- its winning offset changes by no more than one offset bucket between those evaluations.

These are the fixed initial `CONSERVATIVE` profile values. Corpus tests may prove that a profile version must change before release; any changed values require a new algorithm profile identifier and regenerated databases rather than silent runtime drift.

### 7.5 Result confidence and position

After the gates pass:

- `evidence = min(1, aligned_count / 24)`;
- `coverage = min(1, aligned_ratio / 0.30)`;
- `separation = 1` if the runner-up is zero, otherwise `clamp(1 - runner_up / winner, 0, 1)`;
- `confidence = 0.45 * evidence + 0.35 * coverage + 0.20 * separation`.

Confidence is diagnostic; the independent gates determine acceptance.

The winning offset maps session time to cue time. `matchedPositionMs` is the estimated cue position at the callback instant, clamped to the trigger duration.

## 8. Real-time concurrency and memory

Each recognizer instance owns one validated read-only database connection. Each active listening session owns:

- one platform capture source;
- one preallocated single-producer/single-consumer PCM queue;
- one native DSP/matcher worker;
- one read-only database connection;
- one KMP lifecycle controller.

The audio callback only validates the incoming shape and copies/converts into available queue storage. If the queue is full, it increments an atomic dropped-frame counter and returns; it never blocks. Sustained overflow stops the session with a typed audio-overrun error rather than processing stale audio.

The worker owns mutable fingerprint and matcher state. No second worker mutates a session. KMP lifecycle commands are serialized. `stop()` stops capture, prevents new writes, drains or cancels the worker according to session generation, and suppresses late callbacks before completing. `close()` is idempotent and releases all native resources.

Engine working memory, excluding the memory-mapped database file, must remain at or below 16 MB for the MVP profile and must not grow with session duration.

## 9. Database builder

### 9.1 Manifest

```json
{
  "schemaVersion": 1,
  "databaseId": "venue-demo",
  "databaseVersion": "1.0.0",
  "triggers": [
    {
      "id": "welcome-offer",
      "displayName": "Welcome promotion",
      "audio": "audio/welcome-cue.mp3",
      "metadata": {
        "title": "20% off today",
        "body": "Show this offer at checkout",
        "cta": "Claim offer"
      }
    }
  ]
}
```

Rules:

- Manifest and trigger IDs are UTF-8.
- `databaseId`, `databaseVersion`, trigger `id`, `displayName`, and `audio` are required and nonblank.
- Trigger IDs are unique and stable strings of 1 to 128 bytes.
- Audio paths resolve relative to the manifest directory and may not escape it.
- Metadata must be a JSON object with a canonical serialized size no greater than 16 KiB.
- Unknown top-level structural fields are rejected for schema version 1; arbitrary fields are allowed inside `metadata`.

### 9.2 CLI

```text
local-acr-db build --manifest venue-demo.json --output venue-demo.lacrdb
local-acr-db inspect venue-demo.lacrdb
local-acr-db verify venue-demo.lacrdb
```

`build` performs decode, signal validation, fingerprinting, ambiguity analysis, sorted transactional insertion, integrity verification, and atomic replacement of the requested output. It never leaves a partially valid output at the final path.

`inspect` prints database identity/version, schema/profile, triggers, durations, landmark counts/density, validation warnings retained from build, and file size.

`verify` runs SQLite integrity checks and validates required metadata, schema/profile compatibility, foreign-key relationships, counts, and content digest.

Stable exit categories are:

- `0`: success;
- `2`: command/manifest/input validation;
- `3`: audio decode failure;
- `4`: filesystem/database output failure;
- `5`: internal engine failure.

Diagnostics identify the command, trigger ID when applicable, rule, and source path without printing opaque stack traces by default.

### 9.3 Audio validation

The builder rejects:

- duration below 4 seconds or above 60 seconds;
- decoded silence or non-finite PCM;
- fewer than 20 landmarks across the entire cue;
- clipping at or above 5% of decoded samples.

It warns when:

- duration is outside the recommended 5-to-15-second range;
- landmark density is below 8 per second;
- pairwise clean-audio comparison between triggers already passes the runtime acceptance gates, indicating ambiguity.

Warnings are printed and stored in build metadata. Ambiguity is an error because conservative local recognition cannot safely distinguish the cues.

## 10. Database format

`.lacrdb` is a SQLite file built with a pinned amalgamation.

```sql
CREATE TABLE database_metadata (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE triggers (
  trigger_id TEXT PRIMARY KEY,
  display_name TEXT NOT NULL,
  duration_ms INTEGER NOT NULL CHECK(duration_ms > 0),
  metadata_json TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE fingerprints (
  trigger_id TEXT NOT NULL,
  hash INTEGER NOT NULL,
  time_frame INTEGER NOT NULL CHECK(time_frame >= 0),
  PRIMARY KEY(trigger_id, hash, time_frame),
  FOREIGN KEY(trigger_id) REFERENCES triggers(trigger_id)
) WITHOUT ROWID;

CREATE INDEX fingerprints_by_hash
  ON fingerprints(hash, trigger_id, time_frame);
```

Required metadata keys are:

- `schema_version = 1`;
- `algorithm_profile = landmark-v1`;
- `database_id`;
- `database_version`;
- `content_digest_sha256`.

The digest is SHA-256 over length-prefixed canonical UTF-8 metadata, trigger rows, and fingerprint rows sorted by their primary keys. The `content_digest_sha256` metadata row itself is excluded from this input, avoiding a circular digest. It does not hash SQLite page bytes. The builder uses a fixed page size, fixed creation pragmas, sorted insertion, and final `VACUUM`, but the semantic digest—not raw file equality—is the cross-platform reproducibility contract.

At runtime, the engine opens the file read-only and immutable, enables query-only and foreign-key checks, validates metadata/profile, runs an integrity check, and recomputes the content digest before reporting `Ready`. It never writes a journal or modifies the database.

## 11. KMP API and lifecycle

### 11.1 Public types

```kotlin
data class RecognitionConfig(
    val duplicateCooldownMs: Long = 30_000,
    val profile: RecognitionProfile = RecognitionProfile.CONSERVATIVE,
)

data class RecognitionResult(
    val triggerId: String,
    val displayName: String,
    val confidence: Float,
    val matchedPositionMs: Long,
    val metadataJson: String,
)

interface RecognitionListener {
    fun onStateChanged(state: RecognitionState)
    fun onRecognized(result: RecognitionResult)
    fun onError(error: RecognitionError)
}

class LocalAcrRecognizer(
    databasePath: String,
    config: RecognitionConfig = RecognitionConfig(),
) {
    fun prepare(completion: (PrepareResult) -> Unit)
    fun start(listener: RecognitionListener)
    fun stop()
    fun close()
}
```

The final exported Swift names may receive language-idiomatic annotations without changing semantics.

### 11.2 States

`Created → Preparing → Ready → Starting → Listening → Stopping → Ready`

- `Preparing → Failed` on initialization failure.
- `close()` transitions any nonclosed state to `Closed`.
- `stop()` and `close()` are idempotent.
- Other invalid calls produce `InvalidState` and do not mutate the state.
- A stopped recognizer can start a new session without reopening the validated database.

### 11.3 Callback contract

- Listener and completion callbacks are serialized on the platform main thread.
- No recognition callback occurs after `stop()` has completed or after `close()`.
- A listener may call `stop()` from inside a callback without deadlock.
- The same trigger ID is suppressed until its cooldown expires; the cooldown begins when the callback is delivered.
- Different IDs are not mutually suppressed.

### 11.4 Permission and interruption behavior

Native applications request permission before `start()`. The library checks permission/capture availability and reports a typed error before opening the engine when unavailable.

An interruption, route loss, or unrecoverable capture failure stops the current session, returns to `Ready` if the prepared resources remain valid, and emits a recoverable error. The library does not silently resume. The foreground screen may explicitly start again when active.

## 12. Error model

The C ABI returns status codes and fills caller-provided error detail buffers. It catches every C++ exception at the boundary. KMP maps native and platform statuses into stable public categories:

- `DatabaseNotFound`;
- `DatabaseInvalid`;
- `UnsupportedSchema`;
- `UnsupportedAlgorithmProfile`;
- `IntegrityCheckFailed`;
- `MicrophonePermissionDenied`;
- `MicrophoneUnavailable`;
- `AudioInterrupted`;
- `AudioOverrun`;
- `AudioEngineFailure`;
- `InvalidState`;
- `NativeEngineFailure`.

Errors expose a stable code, human-readable message, and `recoverable` flag. Internal context may be logged in debug builds, but raw PCM, full manifest metadata, and promotion content are never logged by the library.

## 13. Sample applications

Both applications show:

- microphone-permission rationale;
- preparation/listening/error status;
- a restrained listening visualization driven by UI animation rather than raw audio data;
- the most recent recognized cue for debugging;
- a native modal promotion built from `metadataJson`;
- a CTA that performs a local sample action and a dismiss button.

The apps automatically prepare the bundled database, request permission, and start while the recognition screen is visible. They stop on screen disappearance. They contain no networking permission, ad network, analytics SDK, or remote content.

Two original generated cue clips and unrelated negative audio are maintained as test/demo fixtures. Copyrighted videos and audio in `examples/` are not copied into product or test assets.

## 14. Testing strategy

Development follows TDD inside every implementation slice:

1. Add one failing behavior or acceptance test.
2. Implement only enough to pass it.
3. Refactor while the relevant suite remains green.
4. Run cross-layer verification before declaring the slice complete.

### 14.1 C++ unit and property tests

- Canonical resampler vectors and chunk-boundary equivalence.
- FFT output against trusted golden vectors within explicit floating-point tolerances.
- Hann window and temporal filter behavior.
- Adaptive threshold decay, provisional peak replacement, and flush behavior.
- Landmark pair ordering, caps, hash layout, and deduplication.
- Ring-buffer wraparound, overflow, and concurrent producer/consumer behavior.
- Offset histogram isolation, unique-vote accounting, gates, confidence, and matched position.
- Database schema/profile/digest/integrity failures.
- Malformed C ABI arguments and exception containment.

### 14.2 Golden parity tests

Canonical PCM fixtures produce the same confirmed peaks, landmarks, hashes, and time frames on macOS, Linux, Android, and iOS. Floating-point FFT/resampler internals use tolerance-based tests; serialized landmark/hash output must be bit-identical.

### 14.3 Robustness corpus

For each known cue, deterministic variants cover:

- start offsets and arbitrary input chunk boundaries;
- gain changes;
- additive crowd-shaped and broadband noise at defined SNRs;
- room echo/impulse response;
- low/high shelf EQ;
- moderate clipping;
- MP3/AAC encode-decode;
- small clock/sample-rate deviations.

Unknown music, speech, ambient venue recordings, silence, and mixtures of unrelated audio form the negative corpus. Corpus parameters and seeds are version-controlled so failures reproduce exactly.

### 14.4 Integration and UI tests

- Manifest-to-database-to-injected-PCM CLI tests.
- KMP lifecycle, cooldown, callback-thread, stop/close, and error-mapping tests using fake platform adapters.
- Android JNI and iOS C-interop smoke tests against the same native vectors.
- Android Compose and iOS XCTest coverage for permission states, listening UI, callback reaction, cooldown, and errors.
- Real-device speaker-to-microphone checks on at least one representative Android device and one supported iPhone before release.

### 14.5 Native safety and performance

- AddressSanitizer and UndefinedBehaviorSanitizer pass on host suites.
- ThreadSanitizer passes concurrency suites where supported.
- Strict compiler warnings and a focused `clang-tidy` ruleset pass.
- Malformed manifest/database property and fuzz tests do not crash or hang.
- A 30-minute injected-audio soak has no memory growth and no PCM loss under nominal scheduling.

## 15. MVP acceptance gates

- At least 95% of positive cases in the defined moderate-noise corpus emit the correct trigger.
- At least 60 minutes of negative corpus audio emit zero callbacks.
- Median recognition latency is at most 3 seconds and p95 is at most 4 seconds on representative devices.
- Serialized landmark/hash golden outputs are bit-identical on all supported targets.
- Prepared engine working memory is at most 16 MB, excluding the memory-mapped database.
- Database preparation for the demo database completes within 500 ms on representative devices.
- The real-time capture path reports no dropped PCM in the 30-minute nominal soak.
- All unit, property, integration, UI, sanitizer, and required real-device checks pass for release.

These gates apply to the version-controlled MVP corpus and device matrix. They are measurable release criteria, not universal claims about arbitrary recordings or environments.

## 16. Security, privacy, and licensing

- Database and manifest parsing treat all lengths, counts, offsets, JSON sizes, and SQLite values as untrusted.
- The builder prevents manifest-relative path traversal.
- The runtime opens only the application-supplied filesystem path and never extracts archives.
- The C ABI uses explicit lengths and opaque handles; no caller-owned pointer is retained beyond its documented call/session lifetime.
- Microphone PCM exists only in bounded memory and is zeroed/released with session buffers where practical.
- Production logging contains state/error codes and performance counters only.
- Third-party sources are pinned, audited, and accompanied by notices. FFmpeg is an external prerequisite and is not redistributed by this project.

## 17. Design consequences

- A database must be rebuilt whenever `algorithm_profile` changes.
- Moving all DSP into C++ makes native tests authoritative and prevents Java/Objective-C/Kotlin fingerprint drift.
- A single immutable SQLite artifact is adequate for MVP scale and leaves room for a future indexed format without changing the public KMP API.
- Conservative multi-gate matching intentionally trades some recall for promotion/check-in safety.
- Foreground-only listening keeps the first release within clear lifecycle, battery, and privacy boundaries.
