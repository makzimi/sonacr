# Local ACR KMP and Mobile Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the shared Kotlin Multiplatform SDK lifecycle layer, then add Android/iOS capture and demo applications that use the same local database artifact.

**Architecture:** Add a root Gradle/Kotlin Multiplatform project with a `shared` SDK module, then add platform application modules in later checkpoints. The shared common code owns public API types, lifecycle serialization, cooldown behavior, callback delivery, typed errors, and small platform abstractions for native session/capture/main dispatch; platform source sets provide actual JNI/cinterop implementations later.

**Tech Stack:** Kotlin Multiplatform, Kotlin/JVM tests for common lifecycle behavior, Android Gradle Plugin for later Android app work, SwiftUI/Xcode project files for later iOS app work, existing native C ABI from `native/core`.

## Global Constraints

- Android minimum API is 26.
- iOS minimum version is 15.
- Raw microphone audio is never persisted, uploaded, or logged.
- KMP shared code contains no FFT, spectrogram, peak, hash, or matching implementation.
- Public asynchronous failures are typed.
- `duplicateCooldownMs` defaults to 30,000 ms and must be in `0..86_400_000`.
- Database paths must be nonblank, contain no embedded NUL, and be no more than 4,096 UTF-8 bytes.
- Listener and completion callbacks are serialized through the platform main dispatcher abstraction.
- `stop()` and `close()` are idempotent.
- A stopped recognizer can start a new session without reopening the database.
- `Failed` is terminal except for `close()`.
- Runtime errors are delivered once through the listener after state returns to `Ready`.

---

## File Structure

- `settings.gradle.kts`: root Gradle project and module inclusion.
- `build.gradle.kts`: root plugin versions and repository policy.
- `gradle.properties`: Kotlin/Gradle defaults.
- `shared/build.gradle.kts`: KMP library targets and source sets.
- `shared/src/commonMain/kotlin/com/localacr/LocalAcrApi.kt`: public API types and factory surface.
- `shared/src/commonMain/kotlin/com/localacr/LocalAcrErrors.kt`: typed public errors and result wrappers.
- `shared/src/commonMain/kotlin/com/localacr/LocalAcrRecognizer.kt`: lifecycle state machine, cooldown, and callback serialization.
- `shared/src/commonMain/kotlin/com/localacr/internal/PlatformPorts.kt`: native session, capture, main dispatcher, and clock abstractions.
- `shared/src/commonTest/kotlin/com/localacr/LocalAcrRecognizerTest.kt`: lifecycle/cooldown/error tests using fake ports.
- `androidApp/`: checkpoint 17 Android demo application.
- `iosApp/`: checkpoint 19 iOS demo application.

## Checkpoint 15: KMP lifecycle API

**Files:**
- Create: `settings.gradle.kts`
- Create: `build.gradle.kts`
- Create: `gradle.properties`
- Create: `shared/build.gradle.kts`
- Create: `shared/src/commonMain/kotlin/com/localacr/LocalAcrApi.kt`
- Create: `shared/src/commonMain/kotlin/com/localacr/LocalAcrErrors.kt`
- Create: `shared/src/commonMain/kotlin/com/localacr/LocalAcrRecognizer.kt`
- Create: `shared/src/commonMain/kotlin/com/localacr/internal/PlatformPorts.kt`
- Create: `shared/src/commonTest/kotlin/com/localacr/LocalAcrRecognizerTest.kt`
- Modify: `docs/superpowers/plans/2026-07-11-local-acr-master-roadmap.md`

**Interfaces:**
- Produces: `LocalAcrFactory.create(databasePath, config, ports)`, `LocalAcrRecognizer.prepare/start/stop/close`, `RecognitionListener`, `RecognitionConfig`, `RecognitionResult`, `RecognitionError`, `CreateResult`, `PrepareResult`, and `OperationResult`.
- Consumes later: Android capture and JNI implementation will implement `NativeSessionPort`, `CapturePort`, `MainDispatcher`, and `MonotonicClock`; iOS cinterop will implement the same ports.

- [x] **Step 1: Add KMP build scaffold**

Create root Gradle files and the `shared` KMP module. The first verified target is JVM-hosted tests for common lifecycle code; Android/iOS source sets are declared in later checkpoints when capture implementations are added.

- [x] **Step 2: Write failing lifecycle tests**

In `shared/src/commonTest/kotlin/com/localacr/LocalAcrRecognizerTest.kt`, add tests that assert:
- factory rejects invalid path and cooldown input with typed errors;
- `prepare` transitions `Created -> Preparing -> Ready` and dispatches completion on the fake main dispatcher;
- `start` transitions `Ready -> Starting -> Listening`;
- duplicate trigger callbacks are suppressed until cooldown expires and different trigger IDs fire immediately;
- `stop` invalidates queued recognition deliveries and completes in `Ready`;
- `close` suppresses queued recognition and error deliveries and completes in `Closed`;
- runtime capture error returns to `Ready` and emits exactly one listener error;
- invalid calls return `InvalidState` without mutating state.

- [x] **Step 3: Run focused red tests**

Run: `./gradlew :shared:jvmTest`

Expected: compile failure because public lifecycle API and port abstractions are absent.

- [x] **Step 4: Implement public API and typed errors**

Implement `RecognitionConfig`, `RecognitionResult`, `RecognitionState`, `RecognitionListener`, result wrappers, `RecognitionErrorCode`, and `RecognitionError`. Keep the public API stable and free of platform classes.

- [x] **Step 5: Implement lifecycle controller with fakeable ports**

Implement `LocalAcrRecognizer` as a serialized state machine over injected ports. The MVP checkpoint uses synchronous fakeable port calls and a `MainDispatcher` queue; Android/iOS capture threads are implemented in later checkpoints.

- [x] **Step 6: Run focused and native suites**

Run:
- `./gradlew :shared:jvmTest`
- `ctest --preset macos-clang-debug --output-on-failure`
- `ctest --preset macos-asan --output-on-failure`
- `ctest --preset macos-tsan --output-on-failure`
- `cmake --build --preset ios-simulator-debug --target local_acr_core`

- [x] **Step 7: Commit checkpoint 15**

Commit: `feat: add shared Local ACR lifecycle API`

## Checkpoint 16: Android capture

**Files:**
- Modify: `shared/build.gradle.kts`
- Create: `shared/src/androidMain/kotlin/com/localacr/android/AndroidLocalAcrPorts.kt`
- Create: `shared/src/androidMain/kotlin/com/localacr/android/AudioRecordCapture.kt`
- Create: `shared/src/androidMain/kotlin/com/localacr/android/NativeSessionJni.kt`
- Create: `shared/src/androidInstrumentedTest/kotlin/com/localacr/android/AudioRecordCaptureTest.kt`
- Modify: native Android CMake/JNI packaging files as needed.

**Interfaces:**
- Consumes: checkpoint 15 `NativeSessionPort`, `CapturePort`, `MainDispatcher`, `MonotonicClock`.
- Produces: Android port factory and JNI direct-buffer binding.

- [ ] Add failing JNI/direct-buffer and capture lifecycle tests.
- [ ] Implement native library loading and JNI wrapper.
- [ ] Implement bounded `AudioRecord` capture thread with permission/capture errors.
- [ ] Verify Android unit/instrumentation target where available and native suites.
- [ ] Commit: `feat: add Android microphone capture`

## Checkpoint 17: Android demo

**Files:**
- Create: `androidApp/build.gradle.kts`
- Create: `androidApp/src/main/AndroidManifest.xml`
- Create: `androidApp/src/main/kotlin/com/localacr/demo/MainActivity.kt`
- Create: `androidApp/src/main/assets/venue-demo.lacrdb`
- Create Compose UI tests.

**Interfaces:**
- Consumes: checkpoint 16 Android shared SDK API.
- Produces: foreground-only permission/listening/promotion demo.

- [ ] Add failing UI tests for permission, listening state, recognized promotion, cooldown display, and local CTA.
- [ ] Implement Compose demo without network/ad/analytics permissions.
- [ ] Verify Android build/tests plus native suites.
- [ ] Commit: `feat: add Android Local ACR demo`

## Checkpoint 18: iOS capture bridge

**Files:**
- Modify: `shared/build.gradle.kts`
- Create: `shared/src/iosMain/kotlin/com/localacr/ios/IosLocalAcrPorts.kt`
- Create: Objective-C++ bridge headers/sources for `AVAudioEngine`.
- Create Kotlin/Native cinterop definition.
- Add compile smoke tests.

**Interfaces:**
- Consumes: checkpoint 15 shared ports and native C ABI.
- Produces: iOS capture bridge that never executes Kotlin on the audio tap.

- [ ] Add failing cinterop/Swift compile contract test.
- [ ] Implement Objective-C++ `AVAudioEngine` bridge and cinterop wrapper.
- [ ] Verify iOS simulator build and native suites.
- [ ] Commit: `feat: add iOS microphone capture bridge`

## Checkpoint 19: iOS demo

**Files:**
- Create: `iosApp/` SwiftUI project files.
- Create bundled `venue-demo.lacrdb` fixture reference.
- Create XCTest coverage.

**Interfaces:**
- Consumes: checkpoint 18 iOS shared SDK framework.
- Produces: SwiftUI permission/listening/promotion demo.

- [ ] Add failing SwiftUI/XCTest coverage for permission, listening state, recognized promotion, cooldown, and local CTA.
- [ ] Implement SwiftUI demo without network/ad/analytics dependencies.
- [ ] Verify iOS simulator build/tests plus native suites.
- [ ] Commit: `feat: add iOS Local ACR demo`

## Self-Review

- Spec coverage: checkpoint 15 covers Section 11 public API, state machine, cooldown, callback serialization, typed error mapping, and fakeable platform abstractions. Checkpoints 16 and 18 cover platform capture; checkpoints 17 and 19 cover demos.
- Placeholder scan: no `TBD`, `TODO`, or unowned implementation steps.
- Type consistency: later checkpoints consume the exact `NativeSessionPort`, `CapturePort`, `MainDispatcher`, and `MonotonicClock` abstractions produced in checkpoint 15.
