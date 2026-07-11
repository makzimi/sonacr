# Local ACR Native Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the portable, deterministic C++ fingerprinting and local-recognition engine through the stable C ABI described by checkpoints 1–11 of the master roadmap.

**Architecture:** Focused C++ components form a one-way streaming pipeline: validated PCM → canonical frames → deterministic spectra → confirmed peaks → landmarks → indexed matcher → session engine. SQLite and third-party DSP code stay behind internal interfaces. A narrow C ABI owns no caller memory, catches all exceptions, and separates real-time PCM production from worker processing and event delivery.

**Tech Stack:** C++20, CMake 3.28+ with CTest, Ninja 1.11+, Apple Clang and Linux Clang, Android NDK, Xcode static libraries, KISS FFT 131.2.0, SpeexDSP 1.2.1, SQLite amalgamation 3.53.3, SHA-256, sanitizers, and clang-tidy.

**Execution preflight:** The current workstation has Apple Clang 21/Xcode 26.5 but does not yet have CMake or Ninja on `PATH`. Before checkpoint 1 begins, install CMake 3.28 or newer and Ninja 1.11 or newer, then record their exact versions in the checkpoint evidence. The repository itself requires only those minimums; CI images pin exact tool versions.

### Pinned source archives

| Dependency | Canonical archive | SHA-256 | Additional upstream check |
|---|---|---|---|
| KISS FFT 131.2.0 | `https://github.com/mborgerding/kissfft/archive/refs/tags/131.2.0.tar.gz` | `205a8f6a448ef12b091f8ac6a514b5091bb5f6b0b543431ed75f673116cf5cbf` | tag `131.2.0` |
| SpeexDSP 1.2.1 | `https://downloads.xiph.org/releases/speex/speexdsp-1.2.1.tar.gz` | `8c777343e4a6399569c72abc38a95b24db56882c83dbdb6c6424a5f4aeb54d3d` | release `1.2.1` |
| SQLite amalgamation 3.53.3 | `https://www.sqlite.org/2026/sqlite-amalgamation-3530300.zip` | `646421e12aac110282ef8cc68f1a62d4bb15fc7b8f09da0b53e29ee690500431` | upstream SHA3-256 `d45c688a8cb23f68611a894a756a12d7eb6ab6e9e2468ca70adbeab3808b5ab9` |

## Global Constraints

- Canonical audio is mono Float32 at 11,025 Hz; FFT window is 512 samples and hop is 128 samples.
- Analysis uses bins 1–255; DC and Nyquist are excluded.
- All discrete fingerprint decisions follow normative `landmark-v1` Sections 19.1–19.5 of the design specification.
- The matcher follows `conservative-v1`, including bounded unique-vote accounting and separate fingerprint/matcher profile identifiers.
- Prepared engine working memory is at most 16 MB under the specification's measurement definition.
- The audio producer performs no allocation, locks, FFT, logging, or database work.
- Android minimum is API 26 and iOS minimum is iOS 15.
- Source from MPL, proprietary, or unlicensed reference projects must not be copied or mechanically translated.
- Production dependencies are pinned by release archive digest with notices, SBOM data, hidden visibility, and recorded compile options.
- Every checkpoint uses red → green → refactor, ends with the full scoped suite passing, and creates exactly one roadmap commit.

---

## Planned native file structure

```text
CMakeLists.txt                         Root project and test options
CMakePresets.json                     macOS/Linux developer and sanitizer presets
cmake/CompilerWarnings.cmake          Warning policy
cmake/Sanitizers.cmake                ASan/UBSan/TSan targets
cmake/StrictFp.cmake                  No-fast-math/no-contraction policy
cmake/Dependencies.cmake              Verified third-party source wiring
third_party/dependencies.lock.json    Versions, URLs, SHA-256, licenses, compile options
native/core/CMakeLists.txt             Core target and hidden dependency targets
native/core/include/local_acr/*.h     Public C ABI only
native/core/src/audio/*               PCM, resampling, framing, timelines
native/core/src/fingerprint/*         FFT, deterministic tables, peaks, landmarks
native/core/src/database/*            SQLite schema, digest, read/write validation
native/core/src/matcher/*             Lookup, alignment, gates, confidence
native/core/src/session/*             Streaming worker, queue, events, lifecycle
native/core/src/support/*             Status, checked arithmetic, SHA-256, diagnostics
native/tests/unit/*                   Focused native unit/property tests
native/tests/golden/*                 Normative vector drivers and small manifests
native/tests/integration/*            Database, session, ABI, concurrency tests
native/tests/support/*                Test assertions, fixtures, fake clocks/sources
```

## Checkpoint 1: Portable native build

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `cmake/CompilerWarnings.cmake`
- Create: `cmake/Sanitizers.cmake`
- Create: `cmake/StrictFp.cmake`
- Create: `cmake/Dependencies.cmake`
- Create: `third_party/dependencies.lock.json`
- Create: `native/core/CMakeLists.txt`
- Create: `native/core/include/local_acr/version.h`
- Create: `native/core/src/support/version.cpp`
- Create: `native/tests/CMakeLists.txt`
- Create: `native/tests/unit/version_test.cpp`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: no earlier production interface.
- Produces: `uint32_t lacr_version_abi(void)` returning `1`; CMake target `local_acr_core`; CTest label `native-unit`.

- [x] **Step 1: Write the first failing native test**

Create `native/tests/unit/version_test.cpp` with a process-returning assertion that includes `local_acr/version.h`, calls `lacr_version_abi()`, and returns nonzero unless the value is exactly `1`.

- [x] **Step 2: Compile directly to verify the red state**

Run: `clang++ -std=c++20 -I native/core/include native/tests/unit/version_test.cpp -o /private/tmp/lacr_version_test`

Expected: compilation fails with `fatal error: 'local_acr/version.h' file not found`, proving the public version contract is absent before the build scaffold is added.

- [x] **Step 3: Add the minimal public version contract and build graph**

Use this exact public contract in `native/core/include/local_acr/version.h`:

```c
#ifndef LOCAL_ACR_VERSION_H
#define LOCAL_ACR_VERSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t lacr_version_abi(void);

#ifdef __cplusplus
}
#endif

#endif
```

Implement `lacr_version_abi()` as an exception-free function returning `1`. Configure `local_acr_core` as a static C++20 library with hidden symbols by default, explicit public include directories, strict warnings, and no compiler extensions. Enable CTest only when `LACR_BUILD_TESTS=ON`.

- [x] **Step 4: Add reproducible presets and verification profiles**

Define `macos-clang-debug`, `macos-asan`, and `macos-tsan` configure/build/test presets using Ninja, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, and separate build directories under `build/`. Apply ASan+UBSan together; apply TSan alone. `StrictFp.cmake` must reject fast-math and add `-ffp-contract=off` to future fingerprint-profile sources.

- [x] **Step 5: Record dependency provenance before compiling dependencies**

Populate `third_party/dependencies.lock.json` with schema version `1` and the exact URLs and digests in the pinned-source table above. Use SPDX `BSD-3-Clause` for KISS FFT and SpeexDSP and `blessing` for SQLite. Notice paths are `third_party/notices/kissfft-COPYING`, `third_party/notices/speexdsp-COPYING`, and `third_party/notices/sqlite-PUBLIC-DOMAIN.txt`. Record scalar Float32/no-OpenMP/no-SIMD for KISS FFT, resampler-only/quality-5 for SpeexDSP, and the Section 10 SQLite compile flags. `cmake/Dependencies.cmake` must fail configuration when an archive digest differs from the lock.

- [x] **Step 6: Run the green build and test**

Run: `cmake --preset macos-clang-debug`

Expected: configuration succeeds with no developer warnings.

Run: `cmake --build --preset macos-clang-debug`

Expected: `local_acr_core` and `version_test` build with zero warnings.

Run: `ctest --preset macos-clang-debug --output-on-failure`

Expected: one `native-unit` test passes.

- [x] **Step 7: Verify and commit checkpoint 1**

Run: `git diff --check`

Expected: no output and exit status `0`.

Commit: `build: bootstrap portable native core`

## Checkpoint 2: PCM ingress

**Files:**
- Create: `native/core/src/audio/pcm_view.hpp`
- Create: `native/core/src/audio/pcm_ingress.hpp`
- Create: `native/core/src/audio/pcm_ingress.cpp`
- Create: `native/core/src/support/status.hpp`
- Create: `native/tests/unit/pcm_ingress_test.cpp`

**Interfaces:**
- Consumes: no platform ABI; checked spans from the test/source adapter. `PcmIngress` receives a caller-owned mono scratch span at construction whose lifetime covers the ingress object; platform PCM pointers are retained only for the duration of `push`.
- Produces: `PcmIngress::push(const PcmView&, MonoSink&) -> Status`, with absolute `first_source_frame`, `frames`, `channels`, `sample_rate`, `SampleFormat`, and plane spans. The sink receives one validated mono batch and is never called for a rejected input.

- [x] **Step 1: Add failing PCM shape and downmix tests**

Cover S16 interleaved mono/stereo, F32 interleaved, F32 planar, channel-order deterministic averaging, clamp behavior, nonfinite rejection, invalid plane counts, zero frames, unsupported rates/channels, and a nonconsecutive `first_source_frame`.

- [x] **Step 2: Run the focused red test**

Run: `cmake --build --preset macos-clang-debug --target pcm_ingress_test`

Expected: compile failure because `PcmView` and `PcmIngress` are undefined.

- [x] **Step 3: Implement checked PCM iteration**

Define `SampleFormat` with exactly `S16Interleaved`, `F32Interleaved`, and `F32Planar`. Validate all counts and byte extents before reading. Sum channels in increasing index order in `double`, multiply once by `1.0 / channels`, convert once to Float32, clamp to `[-1, 1]`, and emit no output when validation fails.

- [x] **Step 4: Implement timeline continuity**

The first accepted batch establishes rate, channels, format, and expected next source frame. Later shape changes return `AudioDiscontinuity`; a frame-index gap returns `AudioDiscontinuity`; exact adjacency updates the expected index with checked arithmetic. `reset()` clears the negotiated shape and timeline.

- [x] **Step 5: Run focused and property tests**

Run: `ctest --preset macos-clang-debug -R pcm_ingress --output-on-failure`

Expected: all PCM examples and deterministic randomized chunk partitions pass.

- [x] **Step 6: Verify and commit checkpoint 2**

Run the full `native-unit` label, then `git diff --check`.

Commit: `feat: add deterministic PCM ingress`

## Checkpoint 3a: Canonical resampling

**Files:**
- Create: `native/core/src/audio/resampler.hpp`
- Create: `native/core/src/audio/speex_resampler.cpp`
- Create: `native/tests/unit/resampler_test.cpp`
- Create: `native/tests/golden/resampler_vectors.json`

**Interfaces:**
- Consumes: mono Float32 batches and their absolute source-frame range.
- Produces: Q1.23-quantized canonical Float32 samples at 11,025 Hz plus input/output latency and source/canonical count mapping.

- [x] **Step 1: Add failing delay, rounding, and chunk-equivalence vectors**

Test 8 kHz, 44.1 kHz, 48 kHz, and 192 kHz inputs; ties-away Q1.23 quantization and saturation; `speex_resampler_skip_zeros`; reported latency; exact `floor(source_frames * 11025 / source_rate)` finite output; and identical canonical samples for one batch versus deterministic random partitions.

- [x] **Step 2: Run the focused resampler RED test**

Run: `cmake --build --preset macos-clang-debug --target resampler_test`

Expected: compilation fails because `CanonicalResampler` does not exist.

- [x] **Step 3: Implement the pinned streaming resampler**

Wrap SpeexDSP 1.2.1 at quality `5`, mono, 11,025 Hz output. Call `speex_resampler_skip_zeros` exactly once. Quantize every output sample to signed Q1.23 with ties away from zero and saturation before returning it as Float32.

- [x] **Step 4: Implement exact finite drain and timeline counts**

Track total accepted source frames with checked arithmetic. During finite finish, feed bounded zero blocks only until Speex emits enough delayed output, retain exactly `floor(source_frames * 11025 / source_rate)` canonical samples, and discard further drain output. Live stop never calls finite finish and therefore invents no samples.

- [x] **Step 5: Run resampler, full, and sanitizer tests**

Run `resampler_test`, the full `native-unit` label, and the ASan/UBSan preset.

Expected: identical canonical sample bytes across chunk partitions, exact finite counts, and no sanitizer findings.

- [x] **Step 6: Verify and commit checkpoint 3a**

Commit: `feat: canonicalize audio with SpeexDSP`

## Checkpoint 3b: Canonical analysis frames

**Files:**
- Create: `native/core/src/audio/frame_stream.hpp`
- Create: `native/core/src/audio/frame_stream.cpp`
- Create: `native/core/src/fingerprint/hann_q31.hpp`
- Create: `native/tests/unit/frame_stream_test.cpp`

**Interfaces:**
- Consumes: ordered Q1.23 canonical samples and their canonical/source timeline mapping.
- Produces: `AnalysisFrame { uint64_t time_frame; std::array<float, 512> samples; }` through a non-owning sink.

- [x] **Step 1: Add failing window, hop, and partial-tail tests**

Test frame zero origin, 512-sample first emission, 128-sample subsequent hops, checked Hann symmetry/endpoints/center coefficients, arbitrary canonical chunking, ring wraparound, source mapping, and discard of every finite tail shorter than a complete window.

- [x] **Step 2: Run the focused frame-stream RED test**

Run: `cmake --build --preset macos-clang-debug --target frame_stream_test`

Expected: compilation fails because `FrameStream` and `AnalysisFrame` do not exist.

- [x] **Step 3: Implement the checked Hann table and bounded ring**

Check in all 512 Q1.31 coefficients generated by `0.5 * (1 - cos(2*pi*n/511))` with ties away from zero. Retain exactly 512 Q1.23 samples in a ring and emit at canonical starts `128*t`; no operation allocates after construction.

- [x] **Step 4: Apply the window and finite rule**

Multiply each Q1.23 sample by `hann_q31[n] / 2^31` in `double`, round once to Float32, and deliver one complete frame to the sink. Finite finish emits no padded frame and discards a partial tail.

- [x] **Step 5: Run focused, full, and sanitizer tests**

Expected: identical frame bytes across every canonical chunk partition and no sanitizer findings.

- [x] **Step 6: Verify and commit checkpoint 3b**

Commit: `feat: produce canonical analysis frames`

## Checkpoint 4: Deterministic spectral decisions

**Files:**
- Create: `native/core/src/fingerprint/spectrum.hpp`
- Create: `native/core/src/fingerprint/spectrum.cpp`
- Create: `native/core/src/fingerprint/log_q16.hpp`
- Create: `native/core/src/fingerprint/log_q16_table.cpp`
- Create: `native/core/src/fingerprint/frequency_weights_q16.hpp`
- Create: `native/core/src/fingerprint/temporal_filter.hpp`
- Create: `native/tests/unit/spectrum_test.cpp`
- Create: `native/tests/unit/log_q16_test.cpp`
- Create: `native/tests/unit/temporal_filter_test.cpp`

**Interfaces:**
- Consumes: `AnalysisFrame`.
- Produces: `DecisionSpectrum { uint64_t time_frame; std::array<int32_t, 255> q16; }` for bins 1–255.

- [ ] **Step 1: Add failing FFT, log, and filter vectors**

Cover impulse/sine/zero frames, excluded DC/Nyquist, binary32 power floor, exponent/mantissa log decomposition, table boundaries, saturating Q16.16 arithmetic, initial zero filtered frame, HPF pole `64225`, and near-threshold values.

- [ ] **Step 2: Run the focused red tests**

Run the three new test targets and confirm missing-type compile failures.

- [ ] **Step 3: Implement scalar spectral power and deterministic log weighting**

Use KISS real FFT with single-precision input and scalar code paths only. Form power in `double`, round once to binary32, clamp to the checked binary32 representation of `1e-12`, index the 65,536-entry Q16.16 mantissa table by the upper fractional bits, then add checked `LN2_Q16` and `LN_WEIGHT_Q16[f]` values with saturation.

- [ ] **Step 4: Implement the temporal recurrence**

Implement `y[t,f] = x[t,f] - x[t-1,f] + mul_q16(64225, y[t-1,f])`, using ties-away multiplication and saturating additions. Initialize `x[-1,f]` from frame zero and `y[-1,f]` to zero.

- [ ] **Step 5: Verify strict-FP compilation and vectors**

Run the focused tests, inspect `compile_commands.json` for `-ffp-contract=off` and absence of fast-math, then run the full native unit suite.

- [ ] **Step 6: Commit checkpoint 4**

Commit: `feat: add deterministic spectral analysis`

## Checkpoint 5: Adaptive peak confirmation

**Files:**
- Create: `native/core/src/fingerprint/gaussian_penalties_q16.hpp`
- Create: `native/core/src/fingerprint/peak_selector.hpp`
- Create: `native/core/src/fingerprint/peak_selector.cpp`
- Create: `native/tests/unit/peak_selector_test.cpp`
- Create: `native/tests/golden/peak_vectors.json`

**Interfaces:**
- Consumes: ordered `DecisionSpectrum` frames.
- Produces: ordered `ConfirmedPeak { uint32_t time_frame; uint8_t bin; int32_t value_q16; }`.

- [ ] **Step 1: Add failing warm-up, masking, tie, and EOF tests**

Cover ten-frame warm-up, checked Gaussian penalties, decay `round_q16(ln(0.997))`, local plateau choosing the lowest bin, descending-value/ascending-bin top-five ordering, newer-wins suppression, exact 22-hop confirmation, bounded provisional storage, and EOF tails from zero through 21 hops.

- [ ] **Step 2: Run the focused red test**

Run the new peak-selector target and confirm missing-type compile failure.

- [ ] **Step 3: Implement fixed-point thresholding and provisional peaks**

Use only Q16.16 values for threshold, sort, suppression, and confirmation decisions. Update thresholds in candidate order and never roll updates back. At age exactly 22, emit unsuppressed peaks and erase all entries of that age.

- [ ] **Step 4: Implement finite/live equivalence**

`finishFinite()` discards every provisional peak younger than 22 frames. Compare a finite builder run with a live run stopped at the same canonical boundary for every varied-tail fixture and require byte-identical confirmed peaks.

- [ ] **Step 5: Run focused, property, full, and ASan suites**

Expected: all order, chunk, tail, and bound properties pass with no growth across a long synthetic stream.

- [ ] **Step 6: Commit checkpoint 5**

Commit: `feat: confirm adaptive spectral peaks`

## Checkpoint 6: Landmark fingerprints

**Files:**
- Create: `native/core/src/fingerprint/landmark.hpp`
- Create: `native/core/src/fingerprint/landmark_builder.hpp`
- Create: `native/core/src/fingerprint/landmark_builder.cpp`
- Create: `native/core/src/fingerprint/fingerprinter.hpp`
- Create: `native/core/src/fingerprint/fingerprinter.cpp`
- Create: `native/tests/unit/landmark_builder_test.cpp`
- Create: `native/tests/integration/fingerprinter_test.cpp`

**Interfaces:**
- Consumes: `ConfirmedPeak` stream or PCM through `Fingerprinter`.
- Produces: `Landmark { uint32_t hash; uint32_t anchor_time_frame; }`, with identity `(hash, anchor_time_frame)`.

- [ ] **Step 1: Add failing hash-layout and ordering tests**

Cover delta times 4 and 96, rejection outside range, bin delta 32 boundary, bins 1 and 255, three-target cap, target ordering, 24-bit packing, reserved upper byte zero, duplicate identity retention, and anchor expiry after all 96-hop targets pass.

- [ ] **Step 2: Run the focused red tests**

Expected: compile failure because `LandmarkBuilder` is absent.

- [ ] **Step 3: Implement bounded pairing and serialization**

Order confirmed peaks by time then bin. For each anchor, inspect targets by delta then bin, emit the first three valid pairs, compute `anchor_bin | (target_bin << 8) | (delta_time << 16)`, and retain the first duplicate identity only.

- [ ] **Step 4: Compose the native fingerprinter**

`Fingerprinter` owns `PcmIngress`, `Resampler`, `FrameStream`, `Spectrum`, `TemporalFilter`, `PeakSelector`, and `LandmarkBuilder`; `reset()` clears every state object; finite finish drains only normative samples/anchors.

- [ ] **Step 5: Run component and PCM-to-landmark goldens**

Run focused tests and the full native suite. Feed identical canonical PCM with multiple input chunkings and require byte-identical landmark sequences.

- [ ] **Step 6: Commit checkpoint 6**

Commit: `feat: generate landmark fingerprints`

## Checkpoint 7: Fingerprint database core

**Files:**
- Create: `native/core/src/database/sqlite_api.hpp`
- Create: `native/core/src/database/schema.hpp`
- Create: `native/core/src/database/database_writer.hpp`
- Create: `native/core/src/database/database_writer.cpp`
- Create: `native/core/src/database/database_reader.hpp`
- Create: `native/core/src/database/database_reader.cpp`
- Create: `native/core/src/database/semantic_digest.hpp`
- Create: `native/core/src/database/semantic_digest.cpp`
- Create: `native/core/src/support/sha256.hpp`
- Create: `native/tests/integration/database_test.cpp`
- Create: `native/tests/golden/schema.sql`

**Interfaces:**
- Consumes: trigger records, canonical metadata JSON, and `Landmark` rows.
- Produces: `DatabaseWriter`, prepared `DatabaseReader`, and `DatabaseIdentity` validated against both profile names and all MVP limits.

- [ ] **Step 1: Add failing schema, digest, corruption, and bound tests**

Cover exact tables/index/pragmas, BINARY sorting, semantic row-order independence, UTF-8 digest fixtures, omitted digest row, invalid schema/profile, foreign-key failure, secondary-index corruption, more than 128 postings per hash, trigger/row/file limits, and read-only connection ownership.

- [ ] **Step 2: Run the focused red integration test**

Expected: compile failure because writer/reader/digest types are absent.

- [ ] **Step 3: Integrate prefixed hidden SQLite**

Generate a prefix header mapping every used `sqlite3_*` export to `lacr_sqlite3_*`; disable loadable extensions; enable serialized mode; hide symbols; assert compile options; and add a link test that also opens system SQLite on Apple.

- [ ] **Step 4: Implement schema, deterministic insertion, and digest**

Create the exact Section 10 schema with fixed creation pragmas. Insert triggers/fingerprints in normative order, omit stop hashes, canonicalize metadata, and hash the exact Section 22 tagged big-endian grammar while excluding `content_digest_sha256`.

- [ ] **Step 5: Implement full preparation validation**

Reject symlinks/nonregular files, open ordinary `mode=ro` with full mutex/query-only, validate metadata and limits before large allocations, run full `integrity_check` plus `foreign_key_check`, recompute the digest, and keep one worker-owned reusable connection.

- [ ] **Step 6: Run database and sanitizer suites**

Expected: every corruption fixture is rejected with a stable status and no database file is modified.

- [ ] **Step 7: Commit checkpoint 7**

Commit: `feat: persist validated fingerprint databases`

## Checkpoint 8: Indexed candidate alignment

**Files:**
- Create: `native/core/src/matcher/query_landmark.hpp`
- Create: `native/core/src/matcher/candidate_lookup.hpp`
- Create: `native/core/src/matcher/candidate_lookup.cpp`
- Create: `native/core/src/matcher/offset_accumulator.hpp`
- Create: `native/core/src/matcher/offset_accumulator.cpp`
- Create: `native/tests/unit/offset_accumulator_test.cpp`
- Create: `native/tests/integration/candidate_lookup_test.cpp`

**Interfaces:**
- Consumes: up to 512 query landmark identities and a prepared `DatabaseReader`.
- Produces: bounded `AlignedCandidate { trigger_id, center_bucket, aligned_query_ids, aligned_count, aligned_ratio }` records.

- [ ] **Step 1: Add failing repeated-hash and negative-offset tests**

Cover one query identity appearing in three neighboring buckets, multiple query times with one hash, negative mathematical floor division, SQL chunks of 256, 128-posting stop limit, 65,536 expansion cutoff, per-trigger isolation, deterministic ties, and secondary centers separated by at least three.

- [ ] **Step 2: Run focused red tests**

Expected: missing candidate-lookup and accumulator types.

- [ ] **Step 3: Implement streaming indexed lookup**

Group query identities by hash, issue parameterized read-only statements in at most 256-hash chunks, stream rows, expand against every query time sharing the hash, and stop without a result when the expansion bound is reached.

- [ ] **Step 4: Implement unique-vote offset aggregation**

Use mathematical floor division by two hops. Score a center by the set union of query identities from center−1 through center+1. Pick by score, absolute offset, signed offset; find the best secondary only among disjoint three-bin ranges.

- [ ] **Step 5: Run focused, full, memory-bound, and ASan tests**

Expected: repeated postings cannot inflate aligned counts and memory remains bounded at maximum profile input.

- [ ] **Step 6: Commit checkpoint 8**

Commit: `feat: align fingerprint candidates`

## Checkpoint 9: Conservative recognition gates

**Files:**
- Create: `native/core/src/matcher/matcher_profile.hpp`
- Create: `native/core/src/matcher/conservative_matcher.hpp`
- Create: `native/core/src/matcher/conservative_matcher.cpp`
- Create: `native/core/src/matcher/recognition_result.hpp`
- Create: `native/tests/unit/conservative_matcher_test.cpp`

**Interfaces:**
- Consumes: current and previous evaluation candidates plus newest analyzed source-frame mapping.
- Produces: optional `NativeRecognitionResult` with trigger ID, confidence, matched cue frame, newest source frame, and diagnostic evidence.

- [ ] **Step 1: Add failing acceptance-boundary tests**

Cover aligned count 11/12, ratio below/at 12%, winner margin 4/5, runner-up ratio below/at 1.25, zero runner-up, secondary-offset 80% ambiguity, consecutive-winner requirement, one/two-bucket offset movement, deterministic trigger-ID tie, confidence endpoints, and out-of-range cue position rejection.

- [ ] **Step 2: Run focused red tests**

Expected: compile failure because `ConservativeMatcher` is absent.

- [ ] **Step 3: Implement the frozen profile data and gates**

Represent the six public gates and internal 80% ambiguity threshold as immutable profile data named `conservative-v1`. Evaluate ambiguity, evidence, ratio, margin, runner-up ratio, consecutive trigger, and offset stability in a fixed order with diagnostics.

- [ ] **Step 4: Implement confidence and position**

Compute the exact evidence/coverage/separation weighted formula from Section 7.5. Map newest analyzed session time through the winning offset, reject out-of-range values, and return source-frame values required for KMP `resultAgeMs`.

- [ ] **Step 5: Run all matcher tests**

Expected: every just-below boundary rejects and every exact boundary accepts only when all other gates hold.

- [ ] **Step 6: Commit checkpoint 9**

Commit: `feat: apply conservative recognition gates`

## Checkpoint 10: Bounded streaming recognition sessions

**Files:**
- Create: `native/core/src/session/recognizer.hpp`
- Create: `native/core/src/session/recognizer.cpp`
- Create: `native/core/src/session/event.hpp`
- Create: `native/tests/integration/recognizer_session_test.cpp`

**Interfaces:**
- Consumes: prepared database, session generation, and PCM batches.
- Produces: bounded native recognition/error events and deterministic session state transitions.

- [ ] **Step 1: Add failing rolling-window and reset tests**

Cover evaluation beginning at two seconds, 22-hop cadence, four-second expiry, 512-query cap, query-density error/reset, two consecutive evaluations, generation tagging, and full DSP/matcher/timeline reset after stop or discontinuity. Cooldown is deliberately excluded here because the KMP controller starts it when the main-thread callback is delivered.

- [ ] **Step 2: Run focused red integration tests**

Expected: missing `Recognizer` and event types.

- [ ] **Step 3: Implement one-worker session ownership**

Keep resampler/fingerprinter/matcher/database mutable state on one worker. Incrementally expire query landmarks, evaluate on the exact cadence, attach generation/source-frame values to events, and reset all session state on stop or typed failure.

- [ ] **Step 4: Implement typed event production**

Emit bounded recognition and terminal session-error records containing the active generation and newest analyzed source-frame index. Do not suppress duplicate trigger events in native code; checkpoint 15 owns delivery-time cooldown in common KMP code.

- [ ] **Step 5: Run injected end-to-end and soak tests**

Use a small generated database and deterministic PCM to verify correct match/no-match/error events, matched position, arbitrary chunking, and constant memory during a 30-minute faster-than-real-time stream.

- [ ] **Step 6: Commit checkpoint 10**

Commit: `feat: run bounded recognition sessions`

## Checkpoint 11: Safe native C ABI

**Files:**
- Create: `native/core/include/local_acr/local_acr.h`
- Create: `native/core/src/session/spsc_pcm_queue.hpp`
- Create: `native/core/src/session/spsc_pcm_queue.cpp`
- Create: `native/core/src/session/c_api.cpp`
- Create: `native/core/src/session/event_codec.hpp`
- Create: `native/core/src/session/event_codec.cpp`
- Create: `native/tests/unit/spsc_pcm_queue_test.cpp`
- Create: `native/tests/integration/c_api_test.c`
- Create: `native/tests/integration/c_api_concurrency_test.cpp`

**Interfaces:**
- Consumes: the exact opaque-handle calls, session generation, and PCM views from Section 18.
- Produces: stable C status codes; `lacr_recognizer_create`, `prepare`, `start_session`, `push_pcm`, `poll_event`, `stop_session`, and `destroy`.

- [ ] **Step 1: Add failing C compilation and ABI behavior tests**

Compile the public header as C11 and C++20. Test null/invalid pointers, embedded-NUL/length limits, create/prepare states, one producer/one poller rule, buffer-too-small retry without consumption, overflow/discontinuity event, exception conversion, stop invalidation, and destroy from Created/Ready/Failed/never-started states.

- [ ] **Step 2: Add failing SPSC wrap/overflow tests**

Cover exact capacity, wraparound, one-second negotiated byte bound, release/acquire visibility, producer overflow without blocking, consumer order, and TSan producer/consumer loops.

- [ ] **Step 3: Implement the queue and versioned event codec**

Preallocate queue storage before listening. Copy PCM during `push_pcm` without allocation or locks. Encode fixed-width event headers plus length-delimited UTF-8 payloads; on insufficient storage report required bytes and consume nothing.

- [ ] **Step 4: Implement ABI containment and lifecycle admission**

Catch all exceptions at every ABI boundary. Enforce serialized control calls, one producer, and one poller. Use an external controller closing gate: admit no new calls after close begins, join producer and poller, wait for admitted calls, then destroy. Treat native runtime errors as session-generation events that the KMP controller can promote to its control epoch.

- [ ] **Step 5: Run C, concurrency, sanitizer, and symbol tests**

Run the C caller suite, native integration suite, ASan+UBSan, TSan, and exported-symbol inspection. Expected: only documented `lacr_*` symbols are externally visible and no race/deadlock is reported.

- [ ] **Step 6: Run the complete native checkpoint suite**

Run: `ctest --preset macos-clang-debug --output-on-failure`

Run: `ctest --preset macos-asan --output-on-failure`

Run: `ctest --preset macos-tsan --output-on-failure`

Expected: all native unit/integration tests pass with zero sanitizer findings.

- [ ] **Step 7: Commit checkpoint 11**

Commit: `feat: expose safe native recognition ABI`

## Native-plan completion review

- [ ] Map every design requirement in Sections 5–8, 10, 12, 14.1–14.2, 14.5, 18, 19, and 22 to a checkpoint above.
- [ ] Search this plan and production diff for unresolved-marker language forbidden by the planning workflow.
- [ ] Verify names and types are consistent from PCM ingress through the C ABI.
- [ ] Update the master roadmap after each commit with its hash and exact verification command results.
- [ ] Do not begin CLI/KMP production work until checkpoint 11 is committed and the native suite is green.
