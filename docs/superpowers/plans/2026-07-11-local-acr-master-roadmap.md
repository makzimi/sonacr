# Local ACR MVP Commit Roadmap

**Design:** `docs/superpowers/specs/2026-07-11-local-acr-mvp-design.md`

**Execution model:** Solo development on the current working branch. Each checkpoint is implemented with TDD, verified, self-reviewed, and committed before the next checkpoint begins. Pull requests, feature toggles, and branch-by-abstraction are not required for the greenfield MVP.

**Rollback model:** Every checkpoint must leave the repository buildable and its scoped tests green. A defect can therefore be investigated with `git bisect`, or a checkpoint can be reverted without depending on uncommitted work from later checkpoints.

## Checkpoint protocol

For every checkpoint:

1. Expand the checkpoint into an executable task plan before changing production code.
2. Add the smallest failing test for one behavior.
3. Run the focused test and record the expected failure.
4. Implement the minimum production behavior.
5. Run the focused test, then the checkpoint suite.
6. Refactor only while the suite remains green.
7. Run `git diff --check`, inspect the complete diff, and verify no unrelated files are included.
8. Commit with the exact message in the table.
9. Record the commit hash and verification evidence in this roadmap.

## Checkpoints

| # | Checkpoint | Deliverable | Depends on | Commit message | Status |
|---|---|---|---|---|---|
| 1 | Portable native build | Reproducible C++20/CMake host and mobile-library build with CTest, warnings, strict FP, sanitizers, and dependency provenance. | — | `build: bootstrap portable native core` | committed |
| 2 | PCM ingress | Validated S16/F32 layouts, deterministic channel downmixing, and source-frame continuity. | 1 | `feat: add deterministic PCM ingress` | committed |
| 3a | Canonical resampling | SpeexDSP resampling, delay compensation, exact finite output, Q1.23 quantization, and chunk invariance. | 2 | `feat: canonicalize audio with SpeexDSP` | committed |
| 3b | Analysis frames | Checked Hann coefficients, 512/128 streaming frames, source mapping, and finite partial-frame discard. | 3a | `feat: produce canonical analysis frames` | committed |
| 4 | Spectral decisions | Scalar KISS FFT power, deterministic logarithmic weighting, and Q16.16 temporal filtering. | 3b | `feat: add deterministic spectral analysis` | committed |
| 5 | Peak confirmation | Adaptive masks, bounded provisional peaks, 22-hop confirmation, and finite/live EOF equivalence. | 4 | `feat: confirm adaptive spectral peaks` | committed |
| 6 | Landmark fingerprints | Stable peak pairing, 24-bit hashes, deterministic ordering, deduplication, and bounded state. | 5 | `feat: generate landmark fingerprints` | committed |
| 7 | Database core | Hidden vendored SQLite schema, canonical metadata/digest, integrity checks, resource bounds, and read-only ownership. | 6 | `feat: persist validated fingerprint databases` | committed |
| 8 | Candidate alignment | Bounded indexed lookup, unique query voting, floor-quantized offsets, and disjoint secondary aggregates. | 7 | `feat: align fingerprint candidates` | committed |
| 9 | Recognition gates | Conservative evidence/separation/stability gates, confidence, ambiguity rejection, and matched position. | 8 | `feat: apply conservative recognition gates` | committed |
| 10 | Streaming engine | Four-second rolling recognition, evaluation schedule, reset, typed events, and injected-PCM tests. | 9 | `feat: run bounded recognition sessions` | committed |
| 11 | Native C ABI | Opaque handles, SPSC queue, push/poll API, session generations, exception containment, and quiescent destruction. | 10 | `feat: expose safe native recognition ABI` | committed |
| 12 | CLI input pipeline | Strict manifest parser and bounded no-shell FFprobe/FFmpeg streaming decoder. | 6 | `feat: decode validated CLI audio inputs` | committed |
| 13 | Database commands | Durable build, inspect, verify, and frozen-toolchain `verify --release` commands. | 7, 12 | `feat: build and verify local ACR databases` | committed |
| 14 | Library ambiguity gate | Exact runtime matcher over every required sliding cross-trigger window during builds. | 9, 13 | `feat: reject ambiguous cue libraries` | committed |
| 15 | KMP lifecycle API | Public types, typed errors, factory, prepare/start/stop/close, main-thread delivery, cooldown, and race tests. | 11 | `feat: add shared Local ACR lifecycle API` | committed |
| 16 | Android capture | JNI direct-buffer binding and bounded `AudioRecord` capture with permission, discontinuity, and shutdown handling. | 15 | `feat: add Android microphone capture` | committed |
| 17 | Android demo | API 26 Compose permission/listening/promotion flow using a bundled generated database. | 13, 14, 16 | `feat: add Android Local ACR demo` | committed |
| 18 | iOS capture | Objective-C++ `AVAudioEngine` bridge and Kotlin/Native integration without Kotlin on the audio tap. | 15 | `feat: add iOS microphone capture bridge` | committed |
| 19 | iOS demo | iOS 15 SwiftUI permission/listening/promotion flow using the same database. | 13, 14, 18 | `feat: add iOS Local ACR demo` | committed |
| 20 | Parity and native safety | Cross-target goldens, Swift ABI gate, malformed-input properties, fuzzing, sanitizers, and nominal soak. | 11, 16, 18 | `test: enforce native parity and safety` | committed |
| 21 | Quality qualification | Preregistered corpus runners, statistical gates, latency/startup/memory benchmarks, and device evidence. | 17, 19, 20 | `test: qualify Local ACR recognition profile` | planned |
| 22 | Release packaging | Android/iOS SDK artifacts, original cue fixtures/database, notices, SBOM, provenance, and release manifest. | 21 | `build: package Local ACR MVP artifacts` | planned |

Status values: `planned · in-progress · committed · blocked`

## Detailed plan set

The roadmap is intentionally navigational. Exact files, interfaces, failing tests, commands, expected output, and implementation snippets are expanded one subsystem ahead of production work:

| Subsystem | Checkpoints | Plan path | State |
|---|---:|---|---|
| Native engine | 1–11 | `docs/superpowers/plans/2026-07-11-local-acr-native-engine.md` | ready for execution |
| CLI and database builder | 12–14 | `docs/superpowers/plans/2026-07-11-local-acr-cli-database.md` | ready for execution |
| KMP and mobile applications | 15–19 | `docs/superpowers/plans/2026-07-11-local-acr-kmp-mobile.md` | completed |
| Qualification and release | 20–22 | `docs/superpowers/plans/2026-07-11-local-acr-qualification-release.md` | ready for execution |

This just-in-time expansion keeps later implementation details aligned with the interfaces and measurements established by earlier committed checkpoints without weakening any commit boundary.

## Checkpoint evidence

| # | Commit | Verification evidence |
|---|---|---|
| 1 | `build: bootstrap portable native core` | Debug, ASan/UBSan, and TSan: 3/3 tests each; iOS simulator static library built; Android API 26 and Linux presets parsed. |
| 2 | `feat: add deterministic PCM ingress` | Focused PCM suite passed; Debug, ASan/UBSan, and TSan: 4/4 tests each; iOS simulator static library built. |
| 3a | `feat: canonicalize audio with SpeexDSP` | Four source rates, exact finite counts, Q1.23 ties/saturation, 24 partition trials, pinned 48 kHz golden vector, and prefixed vendored symbols verified. |
| 3b | `feat: produce canonical analysis frames` | High-precision Hann generator reproduced byte-for-byte; exact 512/128 origins, 32 ring-wrap partition trials, reset, and partial-tail discard verified. |
| 4 | `feat: add deterministic spectral analysis` | KISS FFT archive digest verified; log/weight tables reproduced byte-for-byte; focused spectral vectors, Debug, ASan/UBSan, and TSan suites passed; strict FP flags inspected. |
| 5 | `feat: confirm adaptive spectral peaks` | Gaussian penalties reproduced byte-for-byte; warm-up, plateau ties, top-five ordering, newer suppression, 22-hop confirmation, finite EOF, bounded storage, Debug, ASan/UBSan, and TSan suites passed. |
| 6 | `feat: generate landmark fingerprints` | Hash packing, delta/bin boundaries, duplicate identity retention, anchor expiry, PCM-to-landmark chunk invariance, Debug, ASan/UBSan, and TSan suites passed. |
| 7 | `feat: persist validated fingerprint databases` | SQLite archive digest verified; compile options and Apple system-SQLite coexistence asserted; schema golden, semantic digest, profile/digest/FK corruption, posting bounds, read-only connection, Debug, ASan/UBSan, and TSan suites passed. |
| 8 | `feat: align fingerprint candidates` | Repeated-hash vote deduplication, negative floor offsets, 256-hash SQL chunks, expansion cutoff, per-trigger isolation, disjoint secondary centers, Debug, ASan/UBSan, and TSan suites passed. |
| 9 | `feat: apply conservative recognition gates` | Conservative-v1 evidence, coverage, margin, runner-up ratio, consecutive winner, offset stability, ambiguity, confidence, matched-position boundaries, Debug, ASan/UBSan, and TSan suites passed. |
| 10 | `feat: run bounded recognition sessions` | Injected PCM recognition, generation-tagged events, session restart/reset, query-density terminal error, Debug, ASan/UBSan, and TSan suites passed. |
| 11 | `feat: expose safe native recognition ABI` | C11/C++ ABI callers, SPSC wrap/overflow/concurrency, event codec buffer retry, lifecycle/state errors, exported `lacr_*` symbols, Debug, ASan/UBSan, and TSan suites passed. |
| 12 | `feat: decode validated CLI audio inputs` | Manifest duplicate-key/path/metadata/UTF-8 bounds, FFmpeg argv/no-shell process runner and stdout caps, Debug 24/24, ASan/UBSan 24/24, TSan 24/24, and iOS simulator core build passed. |
| 13 | `feat: build and verify local ACR databases` | CLI build/inspect/verify/release-reject command tests, durable output replacement, build metadata, Debug 25/25, ASan/UBSan 25/25, TSan 25/25, and iOS simulator core build passed. |
| 14 | `feat: reject ambiguous cue libraries` | Build-time ambiguity gate excludes self matches, checks overlapping windows, rejects runtime-passing cross-trigger pairs before output replacement, Debug 26/26, ASan/UBSan 26/26, TSan 26/26, and iOS simulator core build passed. |
| 15 | `feat: add shared Local ACR lifecycle API` | KMP shared JVM lifecycle tests passed via checked-in Gradle wrapper; factory validation, prepare/start/stop/close, main-dispatch, cooldown, stale delivery suppression, runtime error, invalid-state tests covered; native Debug 26/26, ASan/UBSan 26/26, TSan 26/26, and iOS simulator core build passed. |
| 16 | `feat: add Android microphone capture` | Android shared `testDebugUnitTest`, `jvmTest`, and `assembleDebug` passed with rerun-tasks; direct ByteBuffer JNI bridge, AudioRecord direct-buffer capture, source-frame continuity, MIC fallback, and native-event mapping covered; native Debug 26/26, ASan/UBSan 26/26, TSan 26/26, and iOS simulator core build passed. |
| 17 | `feat: add Android Local ACR demo` | Android demo `testDebugUnitTest`, `assembleDebug`, shared `testDebugUnitTest`, and shared `jvmTest` passed with rerun-tasks; permission/listening/promotion/cooldown/local-CTA/observable-state controller tests and unavailable-JNI typed failure covered; generated `venue-demo.lacrdb` inspected as 1 trigger / 74 fingerprints; native Debug 26/26, ASan/UBSan 26/26, TSan 26/26, and iOS simulator core build passed. |
| 18 | `feat: add iOS microphone capture bridge` | Kotlin/Native iOS simulator framework link, Swift bridge header typecheck, Android/shared regressions, native Debug 26/26, ASan/UBSan 26/26, TSan 26/26, and iOS simulator `local_acr_ios_bridge`/`local_acr_core` build passed; Objective-C++ tap pushes PCM directly to native C ABI without Kotlin callback on the audio tap. |
| 19 | `feat: add iOS Local ACR demo` | iOS SwiftUI app/test target built and XCTest ran on iPhone 16 simulator; permission/listening/promotion/cooldown/local-CTA controller tests covered; bundled `venue-demo.lacrdb` inspected as 1 trigger / 74 fingerprints; KMP iOS framework link, shared/Android regressions, native Debug 26/26, ASan/UBSan 26/26, TSan 26/26, and iOS simulator `local_acr_ios_bridge`/`local_acr_core` build passed. |
| 20 | `test: enforce native parity and safety` | Added serialized `landmark-v1` parity golden, native parity CTest, malformed C ABI/input property CTest, nominal injected queue/recognizer soak CTest, and Swift `LACRRecognizer` façade contract XCTest; Debug 29/29, ASan/UBSan 29/29, TSan 29/29, iOS simulator native bridge/core build, Gradle shared/Android regression, and iOS XCTest 7/7 passed. |
| 21–22 | Pending checkpoint execution | Populated immediately after each checkpoint commit. |

## Decisions

- 2026-07-11: The approved design was initially decomposed as review-sized trunk-development slices.
- 2026-07-11: Replaced PR-oriented execution with solo checkpoint commits at the user's request. Isolation, TDD, verification, and rollback boundaries remain; PR mechanics and approval gates were removed.
