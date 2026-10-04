# Local ACR MVP Design

Date: 2026-07-11

Status: Revised after independent review; awaiting approval

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
- MVP database profile: at most 100 triggers, 60 total minutes of source audio, and 1,000,000 stored fingerprint rows. The supplied demo contains only several short clips.
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

No MPL, proprietary, or unlicensed example source will be copied into Local ACR. The new implementation may use public papers, documented algorithms, and behavior-level tests. A provenance ledger records each production file's authorship and any external idea or dependency that influenced it. Third-party production dependencies must have permissive terms, pinned sources, an SBOM entry, and license notices. Distribution requires an appropriate patent and license review.

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
   - Captures microphone PCM with `AudioRecord` on a blocking capture thread and invokes the native C ABI with a reused direct byte buffer and bulk JNI calls.

5. `shared/iosMain`
   - Controls an Objective-C++ capture bridge through Kotlin/Native C interop. The bridge owns `AVAudioEngine`; its real-time input tap calls native queue code directly, so Kotlin/Native never executes on the audio thread.

6. `androidApp`
   - Requests microphone permission, resolves/copies the bundled database to a filesystem path, manages screen lifecycle, collects SDK callbacks, parses the demo promotion metadata, and renders Compose UI.

7. `iosApp`
   - Requests microphone permission, resolves the bundled database path, manages view lifecycle, receives SDK callbacks, parses the demo promotion metadata, and renders SwiftUI.

### 5.2 Dependency direction

- Native UI depends on the shared public KMP API.
- KMP common code depends on small platform abstractions for audio capture, main-thread dispatch, and the native engine session.
- Platform actual implementations depend on platform audio bridges and the C ABI.
- The C++ core depends on internal interfaces for FFT, resampling, database access, clock-free audio processing, and diagnostics.
- The CLI depends on the C++ core; the core never depends on the CLI, KMP, JNI, Swift, or UI.

### 5.3 Native build products

- macOS/Linux: static core library plus `local-acr-db` executable.
- Android: `liblocal_acr.so` for supported ABIs, loaded by the Android KMP implementation.
- iOS: static library slices packaged for device and simulator and linked into the KMP framework/application.

The same public C header is consumed by JNI wrapper code and Kotlin/Native cinterop. Section 18 defines the normative ABI, ownership, and thread contract.

## 6. Audio and fingerprint pipeline

### 6.1 Input boundary

The native engine accepts PCM batches with explicit sample rate, channel count, layout, sample format, and the absolute source-frame index of the first frame. Supported layouts are interleaved signed PCM16, interleaved Float32, and planar Float32. The core deterministically downmixes multiple channels by summing in channel order in `double`, multiplying by `1 / channel_count`, converting to `float`, and clamping to `[-1, 1]`. Canonical resampling remains inside the C++ core.

Android captures PCM16 at a supported native device rate on a blocking `AudioRecord` thread. It prefers `UNPROCESSED` when the device reports support and otherwise uses `MIC`. iOS captures native planar or interleaved Float32 through an Objective-C++ `AVAudioEngine` bridge using `.record` and `.measurement`. The bridge saves the prior audio-session configuration and restores it on stop only when the session still contains the configuration installed by Local ACR.

The CLI invokes FFprobe and FFmpeg directly with argument vectors—never through a shell—to select the first audio stream, validate WAV/MP3 input, decode to native-rate interleaved Float32 while preserving channel count, and feed the core. Downmixing stays in C++. Section 20 defines the hardened decoder contract.

### 6.2 Canonical signal

- Sample rate: 11,025 Hz.
- Channels: one.
- Internal sample type: normalized `float` in `[-1, 1]`.
- FFT window: 512 samples.
- Hop: 128 samples.
- Window: symmetric Hann.
- Analysis band: FFT bins 1 through 255 inclusive. DC bin 0 and Nyquist bin 256 are always excluded.

SpeexDSP 1.2.1 processes platform-native input, and scalar single-precision KISS FFT 131.2.0 processes preallocated 512-sample frames with SIMD/OpenMP disabled. Their release tags, source archive digests, compile options, and license notices are pinned by the dependency lock/provenance file. Both remain hidden behind internal interfaces and can be replaced only under a new fingerprint profile if their discrete golden output changes. Native code compiles with strict IEEE behavior: fast-math is disabled and floating-point contraction is disabled for fingerprint-profile translation units.

### 6.3 Peak selection profile `landmark-v1`

`landmark-v1` applies Hann-windowed power spectra, natural-log compression, frequency weighting, a temporal high-pass response, an adaptive Gaussian threshold, at most five local maxima per frame, and 22-hop delayed confirmation. The decision path quantizes log-spectrum and threshold values to signed Q16.16 fixed point before comparisons; floating-point FFT/resampling differences smaller than a quantization step therefore cannot change discrete decisions.

Section 19 is normative. It defines frame origin, resampler delay removal and flush, partial-frame handling, equations, constants, fixed-point rounding/saturation, threshold initialization, local-maximum neighborhood, comparison and tie rules, mask truncation, provisional-peak replacement, confirmation, and end-of-input behavior. An implementation that differs from Section 19 must use a different fingerprint profile identifier.

### 6.4 Landmarks and hashes

For every confirmed anchor peak:

- consider target peaks 4 through 96 hops later;
- require an absolute frequency-bin delta no greater than 32;
- emit at most three target pairs, ordered by time and then frequency;
- deduplicate identical `(hash, time_frame)` pairs.

The stable 32-bit `landmark-v1` hash packs bins that are normatively limited to `1...255`:

- anchor bin: 8 bits;
- target bin: 8 bits;
- delta time: 8 bits;
- upper 8 bits reserved and set to zero.

The database stores fingerprint and matcher profiles separately. A runtime rejects a database whose fingerprint profile is unsupported or whose matcher profile differs from the release-qualified runtime profile.

## 7. Streaming recognition and matching

### 7.1 Query schedule

- Maintain at most four seconds of query landmarks.
- Begin evaluation once two seconds of audio evidence has been processed.
- Re-evaluate every 22 hops, approximately 255 ms.
- Remove expired samples, frames, peaks, and landmarks incrementally.
- Keep at most 512 query landmarks. If deterministic peak-density limits would exceed this cap, the evaluation returns no match and the session emits `QueryDensityExceeded` before resetting.
  Superseded 2026-10-04: query overflow now keeps the newest 512 landmarks and the session continues (see plan 2026-10-04-local-acr-android-prototype.md, Task 3).

### 7.2 Candidate lookup

Every query landmark has identity `(hash, query_time_frame)`. The matcher builds a map from each distinct hash to all query landmark identities carrying that hash, chunks SQL parameters to at most 256 values per statement, and streams rows from the `fingerprints(hash, trigger_id, time_frame)` index without materializing the complete hit set.

The builder omits a hash from the stored fingerprint table when it would have more than 128 postings across the database and records it as a stop hash in the build report. A runtime evaluation processes at most 65,536 streamed posting/query-time expansions, at most 100 triggers, and bounded sparse accumulators. Exceeding an evaluation bound returns no match and a diagnostic counter; structurally exceeding database limits fails preparation.

### 7.3 Temporal alignment

For each trigger independently:

1. Expand a posting against every query landmark identity with the same hash.
2. Compute signed `database_time - query_session_time` for each expansion.
3. Quantize with mathematical floor division by two hops; negative offsets therefore behave identically on every language/runtime.
4. For each trigger and center bucket, define the aligned set as the set union of query landmark identities appearing in the center bucket and its immediate neighbors. The score is the cardinality of this set, so one query landmark cannot vote twice in the three-bin aggregate.
5. Choose a trigger's best offset by highest score, then smallest absolute offset, then lowest signed offset.
6. Reject the trigger as internally ambiguous when its best secondary offset with a disjoint three-bin aggregate has at least 80% of its winning score. Three-bin aggregates are disjoint only when their center buckets differ by at least three.
7. Choose the winning trigger by score, then aligned ratio, then bytewise trigger ID. The runner-up is the best score from a different trigger.
8. Track the winning trigger and offset from the previous evaluation.

Candidate state is cleared or aged independently; histogram state is never shared between triggers.

### 7.4 Conservative acceptance gates

A trigger is emitted only when all conditions hold:

- at least 12 unique aligned landmarks;
- aligned landmarks are at least 12% of the current unique query landmarks;
- the winner has at least five more aligned landmarks than the runner-up;
- if the runner-up has a nonzero score, the winner/runner-up ratio is at least `1.25`;
- the same trigger wins two consecutive evaluations;
- its winning offset changes by no more than one offset bucket between those evaluations.

These are initial calibration values for matcher profile `conservative-v1`. They become normative only when the preregistered corpus in Section 21 passes and the profile is frozen. Matcher and fingerprint profiles are versioned separately. Changing `landmark-v1` requires database regeneration. Changing only the matcher profile does not change stored hashes, but the builder and runtime must use the same matcher profile for ambiguity validation and release qualification.

### 7.5 Result confidence and position

After the gates pass:

- `evidence = min(1, aligned_count / 24)`;
- `coverage = min(1, aligned_ratio / 0.30)`;
- `separation = 1` if the runner-up is zero, otherwise `clamp(1 - runner_up / winner, 0, 1)`;
- `confidence = 0.45 * evidence + 0.35 * coverage + 0.20 * separation`.

Confidence is diagnostic; the independent gates determine acceptance.

The winning offset maps session time to cue time. `matchedPositionMs` means the cue position at the newest analyzed capture sample—not at main-thread callback delivery. The engine carries absolute capture-frame indices through the queue and removes declared resampler group delay. Each adapter establishes a monotonic timestamp for source-frame zero; native events include the newest analyzed source-frame index. KMP computes `resultAgeMs` from that timeline at main-thread delivery. An out-of-range position invalidates the candidate; clamping must not hide an invalid mapping. Injected tests require absolute position error at or below 250 ms.

## 8. Real-time concurrency and memory

Each recognizer instance owns one validated read-only database connection. Each active listening session owns:

- one platform capture source;
- one preallocated single-producer/single-consumer PCM queue holding one second of the negotiated source format;
- one native DSP/matcher worker;
- one KMP lifecycle controller.

The audio callback only validates the incoming shape and copies/converts into available queue storage. It never blocks. Any queue overflow, nonconsecutive source-frame index, sample-rate change, channel-layout change, or sample-format change marks a discontinuity. The adapter stops with `AudioDiscontinuity` or `AudioOverrun`; the core never concatenates audio across a gap. Starting again creates new resampler, frame, peak, matcher, and timeline state. Recognition deliveries use a session generation, while terminal lifecycle/error deliveries use a separate recognizer control epoch as defined in Sections 11 and 18.

The worker owns mutable fingerprint and matcher state and exclusively owns the reusable database connection while listening. No second worker mutates a session. KMP lifecycle commands are serialized. Section 18 defines generation invalidation, in-flight calls, stop/close completion, queued main-thread delivery cancellation, and destruction ordering.

Engine working memory means peak bytes attributed through the engine allocator plus SQLite's reported heap/page-cache usage after preparation and during the 30-minute soak. It includes PCM queues, DSP/matcher state, SQLite heap, page cache, and allocator fragmentation; it excludes executable code, thread stacks, platform audio objects, and read-only operating-system file cache. This measured total must remain at or below 16 MB for the MVP profile and must not grow with session duration.

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

- Manifest size is at most 1 MiB. Duplicate JSON object keys, invalid UTF-8, NUL bytes, and structural control characters are rejected.
- `databaseId`, `databaseVersion`, trigger `id`, `displayName`, and `audio` are required and nonblank.
- `databaseId`, `databaseVersion`, and trigger IDs use ASCII `[A-Za-z0-9][A-Za-z0-9._-]{0,127}`. Trigger IDs are unique by exact byte value.
- `displayName` is valid UTF-8, contains no NUL/control characters, and is at most 256 bytes. Unicode display and metadata strings are preserved as supplied; the builder does not perform normalization.
- A manifest contains at most 100 triggers and at most 60 aggregate minutes of decoded audio.
- Audio paths resolve relative to the manifest directory. The builder resolves symlinks and requires the canonical input path to remain beneath the canonical manifest directory.
- Only a regular local WAV/PCM or MP3 file is accepted. Each input is at most 100 MiB.
- Metadata must be a JSON object with a canonical serialized size no greater than 16 KiB.
- Unknown top-level structural fields are rejected for schema version 1; arbitrary fields are allowed inside `metadata`.

### 9.2 CLI

```text
local-acr-db build --manifest venue-demo.json --output venue-demo.lacrdb
local-acr-db inspect venue-demo.lacrdb
local-acr-db verify venue-demo.lacrdb
local-acr-db verify --release venue-demo.lacrdb
```

`build` performs decode, signal validation, fingerprinting, ambiguity analysis, sorted transactional insertion, integrity verification, and durable atomic replacement of the requested output. It creates a secure temporary file in the destination directory, rejects output symlinks, verifies the complete file, flushes and `fsync`s it, renames it with explicit overwrite semantics, `fsync`s the destination directory, and cleans the temporary file on every failure. It never leaves a partially valid output at the final path.

`inspect` prints database identity/version, schema/profile, triggers, durations, landmark counts/density, validation warnings retained from build, and file size.

`verify` runs SQLite `integrity_check` and `foreign_key_check` and validates required metadata, schema/profile compatibility, resource counts, build report, and content digest. `verify --release` additionally rejects artifacts whose recorded decoder and builder toolchain do not exactly match the frozen release profile; ordinary `verify` remains useful for development artifacts.

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
- clipping at or above 5% of decoded samples;
- landmark density above 128 per second;
- more than 100,000 fingerprints for one trigger or 1,000,000 rows before stop-hash removal;
- any pair of triggers for which a sliding cross-trigger ambiguity check passes the runtime gates.

It warns when:

- duration is outside the recommended 5-to-15-second range;
- landmark density is below 8 per second.

Warnings are printed and stored in `build_report_json`. Ambiguity checks use every sliding 2-to-4-second window at 255 ms steps, exclude self matches, and execute the exact `conservative-v1` matcher. A hash-intersection prefilter avoids full pairwise work for trigger pairs that share too little evidence. Ambiguity is an error because conservative local recognition cannot safely distinguish the cues.

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
  hash INTEGER NOT NULL CHECK(hash BETWEEN 0 AND 16777215),
  time_frame INTEGER NOT NULL CHECK(time_frame >= 0),
  PRIMARY KEY(trigger_id, hash, time_frame),
  FOREIGN KEY(trigger_id) REFERENCES triggers(trigger_id)
) WITHOUT ROWID;

CREATE INDEX fingerprints_by_hash
  ON fingerprints(hash, trigger_id, time_frame);
```

The builder and runtime enforce: file size at most 256 MiB, at most 100 triggers, at most 1,000,000 fingerprint rows, at most 100,000 rows for one trigger, and at most 128 postings for any stored hash. The runtime validates these bounds before allocating matcher structures.

Every metadata value is at most 64 KiB; trigger `metadata_json` remains limited to 16 KiB. `build_report_json` contains warning records, omitted stop hashes, counts, and toolchain information and must fit the same 64 KiB bound.

Required metadata keys are:

- `schema_version = 1`;
- `fingerprint_profile = landmark-v1`;
- `matcher_profile = conservative-v1`;
- `database_id`;
- `database_version`;
- `build_report_json`;
- `decoder_version`;
- `content_digest_sha256`.

The digest is SHA-256 over the byte grammar in Section 22. The `content_digest_sha256` row itself is excluded, avoiding a circular digest. The digest does not hash SQLite page bytes. The builder uses BINARY collation, a fixed page size, fixed creation pragmas, sorted insertion, and final `VACUUM`. Identical semantic output is guaranteed only for identical inputs built with the pinned Local ACR/decoder toolchain; the semantic digest—not raw SQLite file equality—is the portability contract.

At runtime, the engine canonicalizes the supplied path, rejects symlinks and nonregular files, and opens the private application-owned file with ordinary read-only locking (`mode=ro`, not SQLite immutable mode). It enables query-only and foreign-key checks, validates both profiles and all resource limits, runs full `integrity_check` plus `foreign_key_check`, and recomputes the content digest before reporting `Ready`. Full integrity validation includes secondary-index consistency. It never writes a journal or modifies the database. The application must keep the same private file present and unmodified for the recognizer lifetime.

Preparation is benchmarked against both the demo and a maximum-profile artifact of 100 triggers/1,000,000 rows. It must complete within 500 ms for the demo and two seconds for the maximum artifact on the slowest device in the declared matrix. The builder, explicit `verify` command, and mobile preparation all use full `integrity_check` under the stated time budgets.

The vendored SQLite amalgamation is compiled with loadable extensions disabled, unused features omitted, hidden visibility, and a generated symbol-prefix header that renames every exported `sqlite3_*` symbol used by the amalgamation to `lacr_sqlite3_*`. Link tests include an iOS app that also links system SQLite, preventing symbol collision or accidental cross-binding.

The pinned build uses SQLite serialized threading, opens the connection with full-mutex/read-only flags, and still permits only the engine worker to issue statements while listening. Builder output uses UTF-8, 4,096-byte pages, `auto_vacuum=NONE`, `application_id=0x4C414352`, `user_version=1`, BINARY collation, and final `journal_mode=DELETE`; it closes and removes any transient journal before verification. Feature compile options and creation pragmas are asserted by `verify` and golden-schema tests.

## 11. KMP API and lifecycle

### 11.1 Public types

```kotlin
data class RecognitionConfig(
    val duplicateCooldownMs: Long,
)

data class RecognitionResult(
    val triggerId: String,
    val displayName: String,
    val confidence: Float,
    val matchedPositionMs: Long,
    val resultAgeMs: Long,
    val metadataJson: String,
)

interface RecognitionListener {
    fun onStateChanged(state: RecognitionState)
    fun onRecognized(result: RecognitionResult)
    fun onError(error: RecognitionError)
}

object LocalAcrFactory {
    fun create(databasePath: String): CreateResult
    fun create(databasePath: String, config: RecognitionConfig): CreateResult
}

class LocalAcrRecognizer private constructor() {
    fun prepare(completion: (PrepareResult) -> Unit)
    fun start(listener: RecognitionListener, completion: (OperationResult) -> Unit)
    fun stop(completion: (OperationResult) -> Unit)
    fun close(completion: (OperationResult) -> Unit)
}
```

The one-argument factory uses 30,000 ms. Cooldown must be between zero and 86,400,000 ms inclusive and is measured with a monotonic session clock. Database paths must be nonblank UTF-8 without embedded NUL and no more than 4,096 bytes. Invalid creation input returns a typed error rather than a partially initialized recognizer. `CreateResult`, `PrepareResult`, and `OperationResult` each expose `isSuccess` and exactly one of their success value or typed error.

The iOS framework exposes and compile-tests this Swift surface (names are pinned with Kotlin/Native export annotations where supported):

```swift
let recognizer = try LACRRecognizer(databasePath: databasePath,
                                    duplicateCooldownMs: 30_000)
recognizer.prepare { prepareResult in
    guard prepareResult.isSuccess else { return }
    recognizer.start(listener: listener) { startResult in
        // Listening after success.
    }
}
```

`LACRRecognitionListener`, `LACRRecognitionResult`, `LACRRecognitionError`, and operation-result properties have an Objective-C-compatible representation. A compiling Swift contract test is a release gate. If Kotlin export cannot provide this exact surface on the pinned toolchain, a thin checked-in Swift façade provides it without changing common semantics.

That façade ships in the iOS SDK/sample target rather than existing only as sample code.

### 11.2 States

`Created → Preparing → Ready → Starting → Listening → Stopping → Ready`

- `Preparing → Failed` on initialization failure.
- `close()` transitions any nonclosed state to `Closed`.
- `stop()` and `close()` are idempotent.
- Other invalid calls complete with `InvalidState` and do not mutate the state; no listener is required to observe an operation error.
- A stopped recognizer can start a new session without reopening the validated database.
- `Failed` is terminal except for `close()`; retry uses a new recognizer so no partially validated native state is reused.

### 11.3 Callback contract

- Listener and completion callbacks are serialized on the platform main thread.
- `start` completes only after capture and the native worker enter `Listening`, or after cleanup from a start failure.
- `stop` is asynchronous and may be called inside a listener callback. It first invalidates the recognition-delivery session generation, stops capture, waits for in-flight native pushes and worker polling to quiesce, cancels queued main-thread match deliveries bearing the old generation, transitions to `Ready`, and then calls its completion. No recognition callback from that session occurs after completion.
- `close` follows the same invalidation rules, destroys the native handle after all in-flight calls finish, transitions to `Closed`, and then completes. No listener callback occurs afterward.
- Immediate creation/prepare/start/stop/close failures are reported once through their operation result. Errors that arise after a successful start are reported once through the listener after the old generation is invalidated and state returns to `Ready`; they are not duplicated into an already-completed operation callback.
- Runtime failures are terminal control events tagged with the recognizer control epoch, not the invalidated recognition generation. The serialized lifecycle queue defines the race: if user stop/close is accepted first, it suppresses a later session error; if the failure is accepted first, exactly one error is delivered under the live control epoch after `Ready`, and a concurrent stop completion attaches to that cleanup. Close always suppresses any not-yet-delivered listener error.
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
- `UnsupportedFingerprintProfile`;
- `UnsupportedMatcherProfile`;
- `IntegrityCheckFailed`;
- `MicrophonePermissionDenied`;
- `MicrophoneUnavailable`;
- `AudioInterrupted`;
- `AudioDiscontinuity`;
- `AudioOverrun`;
- `AudioEngineFailure`;
- `QueryDensityExceeded`;
  (Superseded 2026-10-04: query overflow now keeps the newest 512 landmarks and the session continues (see plan 2026-10-04-local-acr-android-prototype.md, Task 3).)
- `ResourceLimitExceeded`;
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
- Finite-input EOF tests prove that the builder emits exactly the peaks and landmarks a live recognizer has confirmed at the same canonical-sample boundary. Fixtures vary cue length and append ambient tails of zero through 21 hops so unconfirmed look-ahead state cannot leak into the database.
- Landmark pair ordering, caps, hash layout, and deduplication.
- Ring-buffer wraparound, overflow, and concurrent producer/consumer behavior.
- Offset histogram isolation, unique-vote accounting, gates, confidence, and matched position.
- Database schema/profile/digest/integrity failures.
- Malformed C ABI arguments and exception containment.

### 14.2 Golden parity tests

Canonical PCM fixtures produce the same confirmed peaks, landmarks, hashes, and time frames on macOS, Linux, Android, and iOS. Floating-point FFT/resampler internals use tolerance-based tests; serialized landmark/hash output must be bit-identical.

### 14.3 Robustness corpus

Before matcher tuning, a checked-in corpus manifest is split by source recording into calibration and untouched holdout sets. Seeds, transforms, device identifiers, playback geometry, and expected outcomes are preregistered. For each known cue, deterministic variants cover:

- start offsets and arbitrary input chunk boundaries;
- gain changes;
- additive crowd-shaped, pink, and broadband noise at 10, 15, 20, and 25 dB SNR;
- room impulse responses with RT60 values from 0.2 through 0.8 seconds;
- low/high shelf EQ;
- moderate clipping;
- MP3/AAC encode-decode;
- small clock/sample-rate deviations.

Positive holdout evaluation contains at least 1,000 trials, at least 25 trials for each trigger in each transform category, uniformly distributed cue start phases across one hop, and uniformly distributed leading offsets from zero through two seconds. It must reach at least 95% overall correct recognition and at least 90% in every trigger/condition bucket.

Unknown music, speech, ambient venue recordings, silence, and mixtures of unrelated audio form the negative corpus. The automated negative holdout contains at least 300 hours: at least 100 hours music, 100 hours speech, and 100 hours venue/ambient mixtures, processed against the full database. Zero callbacks over 300 hours gives an approximate one-sided 95% Poisson upper bound of 0.01 false callbacks per hour. Corpus parameters and seeds are version-controlled so failures reproduce exactly.

### 14.4 Integration and UI tests

- Manifest-to-database-to-injected-PCM CLI tests.
- KMP lifecycle, cooldown, callback-thread, stop/close, and error-mapping tests using fake platform adapters.
- Android JNI and iOS C-interop smoke tests against the same native vectors.
- Android Compose and iOS XCTest coverage for permission states, listening UI, callback reaction, cooldown, and errors.
- Real-device speaker-to-microphone checks on two Android devices (one low/midrange and one current) and two supported iPhones. Checks cover one- and three-meter playback, measured input SNR of at least 10 dB, two ambient conditions from 55 to 70 dBA, and representative rooms within the RT60 range. Each cue has at least 10 positive trials for every device/distance/ambient combination. At least 10 hours of unrelated real acoustic input across the device matrix must emit no callback.

### 14.5 Native safety and performance

- AddressSanitizer and UndefinedBehaviorSanitizer pass on host suites.
- ThreadSanitizer passes concurrency suites where supported.
- Strict compiler warnings and a focused `clang-tidy` ruleset pass.
- Malformed manifest/database property and fuzz tests do not crash or hang.
- A 30-minute injected-audio soak has no memory growth and no PCM loss under nominal scheduling.

## 15. MVP acceptance gates

- Positive holdout accuracy meets the overall and per-bucket requirements in Section 14.3.
- The 300-hour automated negative holdout and 10-hour real-device negative run emit zero callbacks.
- Latency is measured from the first non-silent cue sample entering the native input timeline to delivery of `onRecognized` on the main thread. Median latency is at most 3 seconds and p95 is at most 4 seconds; at least 1,000 injected trials and every positive real-device trial contribute.
- Serialized landmark/hash golden outputs are bit-identical on all supported targets.
- Prepared engine working memory is at most 16 MB under the measurement definition in Section 8.
- Database preparation completes within 500 ms for the demo and two seconds for the maximum-profile artifact on the slowest declared device.
- Absolute `matchedPositionMs` error is at most 250 ms on injected fixtures.
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
- Third-party sources are pinned, audited, recorded in an SBOM/provenance ledger, and accompanied by notices. FFmpeg is an external prerequisite and is not redistributed by this project; the builder records its exact version.
- Source files under unlicensed legacy example directories are analysis references only. New production code must not be copied or mechanically translated from them. Every production algorithm file records new authorship and its public-paper/behavioral references.

## 17. Design consequences

- A database must be rebuilt whenever `fingerprint_profile` changes. A matcher-profile change requires requalification and a builder/runtime version match, but does not change stored landmark hashes.
- Moving all DSP into C++ makes native tests authoritative and prevents Java/Objective-C/Kotlin fingerprint drift.
- A single application-owned, read-only SQLite artifact is adequate for MVP scale and leaves room for a future indexed format without changing the public KMP API.
- Conservative multi-gate matching intentionally trades some recall for promotion/check-in safety.
- Foreground-only listening keeps the first release within clear lifecycle, battery, and privacy boundaries.

## 18. Normative C ABI and thread contract

The checked-in public header follows this shape. Names and fields may be extended only by appending versioned structures; ownership and concurrency semantics are normative.

```c
typedef struct lacr_recognizer lacr_recognizer_t;

typedef enum lacr_sample_format {
  LACR_S16_INTERLEAVED = 1,
  LACR_F32_INTERLEAVED = 2,
  LACR_F32_PLANAR = 3
} lacr_sample_format_t;

typedef struct lacr_pcm_view {
  const void *const *planes;
  uint32_t plane_count;
  uint32_t frames;
  uint32_t channels;
  uint32_t sample_rate;
  uint64_t first_source_frame;
  lacr_sample_format_t format;
} lacr_pcm_view_t;

lacr_status_t lacr_recognizer_create(
    const char *database_path_utf8,
    const lacr_config_t *config,
    lacr_recognizer_t **out,
    lacr_error_buffer_t *error);

lacr_status_t lacr_recognizer_prepare(
    lacr_recognizer_t *recognizer,
    lacr_error_buffer_t *error);

lacr_status_t lacr_recognizer_start_session(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    lacr_error_buffer_t *error);

lacr_status_t lacr_recognizer_push_pcm(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    const lacr_pcm_view_t *pcm);

lacr_status_t lacr_recognizer_poll_event(
    lacr_recognizer_t *recognizer,
    lacr_event_t *event,
    uint8_t *payload,
    size_t payload_capacity,
    size_t *payload_required);

lacr_status_t lacr_recognizer_stop_session(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    lacr_error_buffer_t *error);

void lacr_recognizer_destroy(lacr_recognizer_t *recognizer);
```

Rules:

- `create` validates and copies the database path and configuration before returning; it retains no caller-owned creation pointer.
- `create`, `prepare`, `start_session`, `stop_session`, and `destroy` are serialized on one control worker. `prepare`, `stop_session`, and `destroy` may block that worker but never the platform main or audio thread.
- Exactly one producer thread may call `push_pcm` during a session. Exactly one event worker may call `poll_event`. A status code reports a violated thread/state contract.
- `push_pcm` validates and copies all PCM required by the core before returning. It never retains caller pointers. JNI uses `GetDirectBufferAddress` on a reused direct buffer; it does not use primitive arrays. The Objective-C++ tap passes native buffer pointers directly. Kotlin/Native code never executes in the tap.
- Interleaved input uses `plane_count = 1`. Planar input uses `plane_count = channels`. Each plane must contain the declared frames in the declared format.
- The first successful push establishes sample rate, layout, format, and expected next source-frame index. Any later mismatch or gap invalidates the generation and returns a discontinuity status.
- An overflow or discontinuity also enqueues one generation-tagged session-error event, allowing the event worker to report the failure even when the producer is the Objective-C++ bridge.
- `poll_event` copies a versioned binary event header and length-delimited UTF-8 fields into caller storage. If capacity is insufficient, it returns `BUFFER_TOO_SMALL`, reports the required size, consumes nothing, and permits retry. Returned strings are never engine-owned pointers.
- The controller owns a closing gate outside the raw C handle. Once closing begins, the gate admits no new ABI entry. It then stops and joins the Objective-C++/Android producer, stops and joins the event poller, and waits for every call admitted before the gate to leave the ABI. Internal in-flight references protect only those already-admitted calls; they do not make a raw pointer safe after destroy begins.
- Only after that quiescence may the serialized control worker call `destroy`. The rule applies from every state, including `Created`, `Ready`, `Failed`, and a recognizer that never started a session. `destroy` therefore never races with `push_pcm`, `poll_event`, or another control call, and no caller may enter the ABI with the pointer afterward.
- Native match and session-error events originate with the active session generation. The KMP controller retains that generation on queued recognition deliveries and checks it again on main-thread delivery; stop/close invalidation discards stale recognition deliveries.
- A terminal session error is different: it carries its originating generation only until the serialized lifecycle queue adjudicates the race in Section 11.3. If the error wins, the controller invalidates that session generation, promotes exactly one typed error and the resulting `Ready` state transition to the current recognizer control epoch, and queues them in state-then-error order. If stop/close wins, it suppresses the native error. Control-epoch validation at main-thread delivery lets `close` suppress a promoted error that has not yet been delivered without accidentally discarding an accepted error merely because its session was invalidated.
- The Objective-C++ capture bridge owns `AVAudioEngine` and audio-session changes. It exposes start/stop functions through a separate C header, holds no Kotlin object, and writes only to `lacr_recognizer_push_pcm`.
- All C++ exceptions are caught at the outermost ABI function. Status and error buffers contain stable codes and bounded UTF-8 diagnostic text.

## 19. Normative `landmark-v1` fingerprint profile

### 19.1 Resampling, samples, and frames

- Downmixed samples are processed by SpeexDSP resampler 1.2.1, quality 5, one channel, with `speex_resampler_skip_zeros` called once after initialization.
- Resampled output is rounded to signed Q1.23 using round-to-nearest with ties away from zero, saturated, then converted back to `float` as `q / 2^23` for FFT input.
- A checked-in resampler vector suite is normative. A platform whose output differs by enough to change Q1.23 samples fails parity and is unsupported until the fingerprint profile changes.
- The resampler reports input/output latency. Timeline mapping subtracts output latency. At end of a finite CLI input, the builder drains the resampler with zeros but retains exactly `floor(source_frame_count * 11025 / source_rate)` canonical samples after delay compensation. Live stop does not invent trailing audio.
- Canonical sample zero is the first delay-compensated output sample. FFT frame `t` starts at canonical sample `128 * t` and consumes 512 samples. A finite trailing partial frame is discarded, never zero-padded.
- The 512 Hann coefficients are generated once as `0.5 * (1 - cos(2*pi*n/511))`, rounded to Q1.31 with ties away from zero, checked into the repository as a normative table, and used identically on every target.
- FFT input multiplies each Q1.23 sample by the corresponding `hann_q31 / 2^31` in `double`, rounds once to `float`, and uses that value as the KISS FFT input.

### 19.2 Spectrum and deterministic decision values

For bins `f = 1...255`:

1. Compute scalar KISS real FFT output using single-precision input and the pinned implementation.
2. Compute `power = re[f] * re[f] + im[f] * im[f]` in `double`, round once to IEEE-754 binary32, and clamp to the checked-in binary32 constant representing `1e-12`.
3. Do not call platform `log` for a decision value. Decompose the positive binary32 power into exponent and mantissa bits. Use the upper 16 fractional mantissa bits to index a checked-in Q16.16 table for `ln(1 + i/65536)`, add `exponent * LN2_Q16`, then add the checked-in `LN_WEIGHT_Q16[f] = round_q16(ln(f+16))`. This is equivalent to multiplying magnitude by `sqrt(f + 16)` before forming power, with deterministic log quantization.
4. Apply the temporal high-pass recurrence with saturating Q16.16 arithmetic: `y[t,f] = x[t,f] - x[t-1,f] + mul_q16(HPF_POLE_Q16, y[t-1,f])`, where `HPF_POLE_Q16 = 64225` and `mul_q16` rounds to nearest with ties away from zero. At `t = 0`, set `x[-1,f] = x[0,f]` and `y[-1,f] = 0`, so the first filtered frame is zero.

All peak, threshold, sorting, suppression, and confirmation decisions use Q16.16 integers only. Fast-math and FMA contraction are disabled in these translation units. Near-threshold and plateau golden vectors are required in addition to ordinary audio vectors.

### 19.3 Warm-up and adaptive threshold

- Collect filtered frames `t = 0...9` without emitting peaks.
- For each bin, `m[f]` is the maximum filtered value across those ten frames.
- For a source bin `g`, define `sigma(g) = 3 * sqrt(g + 3)`. The normative implementation uses a checked-in Q16.16 table for every `(g,f)` Gaussian log penalty: `G[g,f] = round_q16(-0.5 * ((f-g)/sigma(g))^2)`. Values below `-16.0` are clamped to `-16.0`.
- Initialize `threshold[f] = max_g(m[g] + G[g,f])` with saturating addition.
- The per-frame decay constant is a checked-in Q16.16 value equal to `round_q16(ln(0.997))`. At the end of every processed frame, add this constant to every threshold bin with saturation.

### 19.4 Candidate peaks and confirmation

For every frame after warm-up:

1. Bin `f` is a local maximum when `y[f] > threshold[f]`, `y[f] > y[f-1]`, and `y[f] >= y[f+1]`. Out-of-band neighbors are negative infinity. This chooses the lowest bin of a flat plateau.
2. Sort candidates by descending `y`, then ascending bin. Keep the first five.
3. For each kept peak `(t,g,v)` in that order, update every threshold bin with `max(threshold[f], v + G[g,f])`.
4. Store each kept peak as provisional. A newer peak `(tn,gn,vn)` suppresses an older provisional peak `(to,go,vo)` when `vo <= vn + G[gn,go] + (tn-to) * decay_q16`. Equal comparisons favor the newer peak; within one frame the earlier candidate ordering is authoritative.
5. After processing frame `t`, confirm every unsuppressed provisional peak whose age is exactly 22 frames and remove all suppressed/confirmed entries of that age. This yields a fixed 22-hop look-ahead.
6. At finite end-of-input, discard every remaining provisional peak younger than 22 frames. EOF is not synthetic evidence: only peaks that completed the same 22-hop look-ahead used by the live recognizer may enter a database. Consequently, a finite builder run and a live recognizer stopped at the same canonical-sample boundary expose identical confirmed peaks.

Threshold updates are not rolled back when a provisional peak is later suppressed; they represent the online masking history. Chunk boundaries do not reset any state. Feeding the same source as one batch or arbitrary nonempty batches must produce identical confirmed peaks.

### 19.5 Pairing and serialization

- Confirmed peaks are ordered by time then bin.
- For each anchor, examine targets with delta time `4...96` and absolute bin delta at most 32, ordered by delta time then target bin. Emit the first three.
- The hash is `anchor_bin | (target_bin << 8) | (delta_time << 16)`. Bins are `1...255`; the upper byte is zero.
- A landmark identity in serialized output is `(hash, anchor_time_frame)`. Duplicate identities are removed by retaining the first generated occurrence.
- Anchor state is retained until all possible 96-hop targets have passed. Finite flush pairs anchors only with targets that actually exist.

## 20. Hardened decoder and builder process contract

- The supported release toolchain pins one exact FFmpeg/FFprobe 8.1.x patch build, configuration, and executable digest in CI and the documented build container. Other local 8.0/8.1 builds are development-only and record their exact version/configuration; `verify --release` rejects their artifacts. Byte-identical semantic output is promised only with the frozen release toolchain.
- FFprobe executes directly with an argument vector to read the first audio stream's codec, container, channels, duration, and sample rate. Inputs outside WAV with PCM audio or MP3 audio are rejected.
- FFmpeg executes directly—never through a shell—with `-nostdin`, error-only logging, first-audio-stream mapping, disabled video/subtitle/data streams, local `file,pipe` protocols only, Float32 little-endian interleaved output preserving the source channel count, and no output sample-rate conversion. The builder accepts 1 through 8 channels and sample rates from 8,000 through 192,000 Hz; the C++ core performs downmix and resampling.
- The process receives no inherited interactive stdin. The builder caps captured stderr at 64 KiB, decoded output at 400 MiB (60 seconds × 192 kHz × 8 Float32 channels plus framing allowance), wall time at 30 seconds per clip, and input size at 100 MiB. PCM is streamed to the core rather than accumulated. Exceeding any limit terminates the child and returns decode error 3.
- Environment-dependent search uses an explicitly resolved executable path. The tool prints and stores the resolved FFmpeg version. The manifest and audio paths are passed as individual arguments and never interpolated into a command string.
- The builder's canonical JSON parser rejects duplicate keys and nonfinite numbers. Metadata JSON is reserialized with keys sorted by UTF-8 bytes, no insignificant whitespace, shortest round-trippable JSON numbers, and preserved UTF-8 string bytes.

## 21. Matcher calibration and quality protocol

- `landmark-v1` discrete parity is established before matcher tuning.
- The calibration corpus may tune matcher gate values. The holdout corpus and negative-duration gate are not inspected during tuning.
- Freezing `conservative-v1` records its six gate constants, confidence formula, corpus manifest digest, decoder version, device matrix, and result metrics in a checked-in profile document.
- Any later gate change creates a new matcher profile and reruns ambiguity validation and the full holdout protocol. It does not silently reuse the `conservative-v1` name.
- Injected processing may run faster than wall clock but preserves source timestamps, evaluation cadence, main-thread delivery simulation, and four-second rolling-window semantics.
- Real acoustic latency uses a synchronized playback/capture marker. Injected latency begins at the first non-silent cue sample entering the native source timeline. Both end at main-thread callback delivery.
- Moderate acoustic conditions mean measured input SNR of 10 through 25 dB, RT60 of 0.2 through 0.8 seconds, and the playback/ambient ranges in Section 14.4.

## 22. Canonical semantic digest grammar

All integers are unsigned big-endian. All strings are their stored UTF-8 bytes without normalization. Lengths are 32-bit byte counts. SQLite sorting uses BINARY byte ordering.

The SHA-256 input is:

1. Domain bytes `LACRDB-DIGEST`, followed by byte `0x00` and `u32(1)`.
2. Every metadata row except `content_digest_sha256`, sorted by key bytes:
   - tag `0x01`;
   - `u32(key_length)`, key bytes;
   - `u32(value_length)`, value bytes.
3. Every trigger row sorted by `trigger_id` bytes:
   - tag `0x02`;
   - length-prefixed trigger ID;
   - length-prefixed display name;
   - `u64(duration_ms)`;
   - length-prefixed canonical metadata JSON.
4. Every fingerprint row sorted by `(trigger_id, hash, time_frame)` under BINARY/integer order:
   - tag `0x03`;
   - length-prefixed trigger ID;
   - `u32(hash)`;
   - `u32(time_frame)`.
5. Final byte `0xFF`.

The builder and verifier share one digest implementation. Golden fixtures cover empty optional metadata, multibyte UTF-8 display text, numeric JSON values, maximum lengths, and row-order permutations.
