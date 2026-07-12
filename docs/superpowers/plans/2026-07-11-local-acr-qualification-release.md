# Local ACR Qualification and Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add enforceable parity, native-safety, qualification, and release-packaging gates for the Local ACR MVP after both demo applications exist.

**Architecture:** Checkpoint 20 adds automated safety gates that can run on every development machine: serialized native goldens, a checked Swift façade contract, malformed-input/property tests, sanitizer-backed fuzz-style tests, and a bounded nominal soak. Checkpoint 21 adds preregistered qualification manifests and deterministic injected-corpus runners; real-device evidence is recorded as versioned artifacts without blocking ordinary unit-test runs. Checkpoint 22 packages Android/iOS/native/CLI artifacts with notices, provenance, SBOM, and a release manifest.

**Tech Stack:** C++20/CMake/CTest, Kotlin Multiplatform/Gradle, Swift/XCTest/Xcode, JSON fixture manifests, shell-free native CLI tooling.

## Global Constraints

- Android minimum is API 26.
- iOS minimum version is 15.
- Raw microphone audio is never persisted, uploaded, or logged.
- Runtime database download, replacement, networking, ad networks, and analytics are excluded.
- `landmark-v1` serialized landmark/hash output must be bit-identical across supported targets.
- AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer suites must pass before checkpoint completion.
- Malformed manifest/database/property/fuzz tests must not crash or hang.
- A nominal injected-audio soak must report no memory growth and no PCM loss under bounded scheduling.
- Release packaging must not copy copyrighted or unlicensed material from `examples/`.

---

## File Structure

- `native/tests/golden/parity_landmarks.json`: checked deterministic serialized landmark/hash vectors for `landmark-v1`.
- `native/tests/integration/parity_goldens_test.cpp`: host CTest gate that regenerates landmarks from deterministic PCM under several chunk/sample-format paths and compares serialized output to `parity_landmarks.json`.
- `native/tests/integration/malformed_input_property_test.cpp`: deterministic property-style malformed C ABI and database-input tests.
- `native/tests/integration/nominal_soak_test.cpp`: bounded injected recognizer/queue soak with no PCM loss and stable counters.
- `native/tests/CMakeLists.txt`: adds the new checkpoint 20 tests to CTest labels.
- `iosApp/LocalAcrDemo/LACRRecognizer.swift`: thin Swift façade that presents the spec-pinned `LACRRecognizer(databasePath:duplicateCooldownMs:)` surface over `LocalAcrShared`.
- `iosApp/LocalAcrDemoTests/LACRRecognizerContractTests.swift`: XCTest compile/runtime gate for the Swift façade contract using invalid path and cooldown validation.
- `iosApp/LocalAcrDemo.xcodeproj/project.pbxproj`: includes the façade and contract test.
- `docs/qualification/conservative-v1-profile.md`: matcher profile freeze template populated with current constants, required evidence fields, and pending checkpoint 21 corpus evidence slots.
- `docs/release/local-acr-mvp-release-manifest.md`: checkpoint 22 release manifest template and artifact checklist.
- `docs/superpowers/plans/2026-07-11-local-acr-master-roadmap.md`: status and verification evidence updates.

## Checkpoint 20: Parity and native safety

**Files:**
- Create: `native/tests/golden/parity_landmarks.json`
- Create: `native/tests/integration/parity_goldens_test.cpp`
- Create: `native/tests/integration/malformed_input_property_test.cpp`
- Create: `native/tests/integration/nominal_soak_test.cpp`
- Modify: `native/tests/CMakeLists.txt`
- Create: `iosApp/LocalAcrDemo/LACRRecognizer.swift`
- Create: `iosApp/LocalAcrDemoTests/LACRRecognizerContractTests.swift`
- Modify: `iosApp/LocalAcrDemo.xcodeproj/project.pbxproj`
- Create: `docs/qualification/conservative-v1-profile.md`
- Modify: `docs/superpowers/plans/2026-07-11-local-acr-master-roadmap.md`

**Interfaces:**
- Consumes: `local_acr::Fingerprinter`, `local_acr::Recognizer`, C ABI `lacr_*` functions, KMP `LocalAcrFactory`, and the existing iOS demo target.
- Produces: enforceable CTest/XCTest gates for serialized parity, malformed input safety, nominal soak behavior, and Swift API shape.

- [x] **Step 1: Add failing native parity golden test**

Create `native/tests/integration/parity_goldens_test.cpp` with a deterministic PCM generator and a comparison against `native/tests/golden/parity_landmarks.json`. The test must:
- generate six seconds of deterministic mono Float32 PCM at 11,025 Hz;
- feed it as one batch, uneven chunks, and interleaved S16 converted from the same source;
- serialize each emitted landmark as `{ "hash": <uint32>, "timeFrame": <uint32> }`;
- require all paths to match the checked golden exactly.

Add the executable to `native/tests/CMakeLists.txt`:

```cmake
add_executable(parity_goldens_test integration/parity_goldens_test.cpp)
target_link_libraries(parity_goldens_test PRIVATE local_acr_core)
target_include_directories(parity_goldens_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(parity_goldens_test PRIVATE cxx_std_20)
lacr_apply_warnings(parity_goldens_test)
lacr_apply_sanitizers(parity_goldens_test)
lacr_apply_strict_fp(parity_goldens_test)

add_test(NAME parity_goldens_test COMMAND parity_goldens_test)
set_tests_properties(parity_goldens_test PROPERTIES LABELS "native-integration;parity")
```

Run: `cmake --build --preset macos-clang-debug --target parity_goldens_test && ctest --test-dir build/macos-clang-debug -R parity_goldens_test --output-on-failure`

Expected: FAIL because `parity_landmarks.json` does not exist yet.

- [x] **Step 2: Generate and check in the golden vector**

Run the parity test locally with an explicit `--write-golden native/tests/golden/parity_landmarks.json` mode implemented only in the test binary. Re-run without `--write-golden`.

Expected: PASS and at least 12 serialized landmarks.

- [x] **Step 3: Add failing malformed-input/property test**

Create `native/tests/integration/malformed_input_property_test.cpp`. It must run a deterministic set of malformed C ABI cases:
- null path/config/out pointers;
- wrong ABI version;
- empty and over-4096-byte paths;
- zero input sample rate;
- null PCM plane array;
- wrong planar/interleaved `plane_count`;
- null plane pointer;
- zero frames/channels;
- invalid sample format value;
- stale generation push after stop;
- polling with insufficient event payload capacity and retrying.

Add it to CTest with labels `native-integration;safety`.

Run: `cmake --build --preset macos-clang-debug --target malformed_input_property_test && ctest --test-dir build/macos-clang-debug -R malformed_input_property_test --output-on-failure`

Expected: FAIL on at least one missing validation case.

- [x] **Step 4: Harden native validation only as needed**

Update the smallest required C ABI validation logic in `native/core/src/session/c_api.cpp`. The target behavior is:
- invalid enum values return `LACR_STATUS_INVALID_ARGUMENT`;
- empty paths and paths longer than 4,096 bytes return `LACR_STATUS_INVALID_ARGUMENT`;
- stale generation pushes return `LACR_STATUS_INVALID_STATE`;
- event retry after `BUFFER_TOO_SMALL` consumes nothing.

Run the malformed-input test again.

Expected: PASS.

- [x] **Step 5: Add failing nominal soak test**

Create `native/tests/integration/nominal_soak_test.cpp`. It must:
- exercise `SpscPcmQueue` with 30 minutes of synthetic 20 ms chunks using a small bounded queue;
- assert every pushed chunk is popped in order with no generation loss;
- run the recognizer over repeated deterministic cue audio for a bounded number of chunks sufficient to cross many evaluation intervals;
- assert no `SessionError` is emitted during nominal input.

Add it to CTest with labels `native-integration;soak`.

Run: `cmake --build --preset macos-clang-debug --target nominal_soak_test && ctest --test-dir build/macos-clang-debug -R nominal_soak_test --output-on-failure`

Expected: FAIL until the test is wired and any required counters are exposed.

- [x] **Step 6: Implement the minimal soak support**

Prefer test-only helper code inside `nominal_soak_test.cpp`. Do not add production counters unless the test cannot observe the behavior otherwise. Re-run the soak test.

Expected: PASS under the debug preset within ordinary unit-test time.

- [x] **Step 7: Add failing Swift façade contract test**

Create `iosApp/LocalAcrDemoTests/LACRRecognizerContractTests.swift`:

```swift
import XCTest
@testable import LocalAcrDemo

final class LACRRecognizerContractTests: XCTestCase {
    func testFacadeRejectsInvalidCooldownWithTypedError() {
        XCTAssertThrowsError(try LACRRecognizer(databasePath: "/tmp/missing.lacrdb", duplicateCooldownMs: -1)) { error in
            XCTAssertTrue(String(describing: error).contains("duplicateCooldownMs"))
        }
    }

    func testFacadeCanBeConstructedWithSpecSurface() throws {
        let recognizer = try LACRRecognizer(databasePath: "/tmp/missing.lacrdb", duplicateCooldownMs: 30_000)
        XCTAssertNotNil(recognizer)
    }
}
```

Run: `xcodebuild test -quiet -project iosApp/LocalAcrDemo.xcodeproj -scheme LocalAcrDemo -destination 'platform=iOS Simulator,id=<available-iPhone-simulator-id>' -derivedDataPath /private/tmp/local-acr-ios-derived CODE_SIGNING_ALLOWED=NO ARCHS=arm64 ONLY_ACTIVE_ARCH=YES`

Expected: FAIL because `LACRRecognizer` does not exist.

- [x] **Step 8: Implement Swift façade**

Create `iosApp/LocalAcrDemo/LACRRecognizer.swift` with:
- `public final class LACRRecognizer`;
- initializer `init(databasePath: String, duplicateCooldownMs: Int64 = 30_000) throws`;
- `prepare(_:)`, `start(listener:_:)`, `stop(_:)`, and `close(_:)` methods that delegate to `LocalAcrShared.LocalAcrRecognizer`;
- lightweight `LACRRecognitionListener`, `LACRRecognitionResult`, and `LACRRecognitionError` type aliases or wrappers when the Kotlin export already provides compatible Objective-C-visible types.

Re-run the XCTest command.

Expected: PASS.

- [x] **Step 9: Add profile freeze template**

Create `docs/qualification/conservative-v1-profile.md` recording current matcher constants:
- minimum aligned landmarks: 12;
- minimum aligned ratio: 0.12;
- minimum winner margin: 5;
- minimum winner/runner-up ratio: 1.25;
- consecutive winner count: 2;
- maximum offset bucket drift: 1;
- confidence formula: `0.45 * evidence + 0.35 * coverage + 0.20 * separation`.

Mark corpus metrics as “pending checkpoint 21” rather than inventing results.

- [x] **Step 10: Run checkpoint 20 verification**

Run:
- `cmake --build --preset macos-clang-debug && ctest --preset macos-clang-debug --output-on-failure`
- `cmake --build --preset macos-asan && ctest --preset macos-asan --output-on-failure`
- `cmake --build --preset macos-tsan && ctest --preset macos-tsan --output-on-failure`
- `cmake --build --preset ios-simulator-debug --target local_acr_ios_bridge local_acr_core`
- `ANDROID_HOME=/Users/maxkach/Library/Android/sdk GRADLE_USER_HOME=/private/tmp/local-acr-gradle-home ./gradlew :shared:linkDebugFrameworkIosSimulatorArm64 :shared:jvmTest :shared:testDebugUnitTest :androidApp:testDebugUnitTest :androidApp:assembleDebug --rerun-tasks --no-daemon`
- `xcodebuild test -quiet -project iosApp/LocalAcrDemo.xcodeproj -scheme LocalAcrDemo -destination 'platform=iOS Simulator,id=<available-iPhone-simulator-id>' -derivedDataPath /private/tmp/local-acr-ios-derived CODE_SIGNING_ALLOWED=NO ARCHS=arm64 ONLY_ACTIVE_ARCH=YES`
- `git diff --check`

- [x] **Step 11: Commit checkpoint 20**

Update roadmap status/evidence and commit:

```bash
git add native/tests iosApp docs/qualification docs/superpowers/plans/2026-07-11-local-acr-master-roadmap.md docs/superpowers/plans/2026-07-11-local-acr-qualification-release.md
git commit --author="Maxim Kachinkin <m.kachinkin@dodobrands.io>" -m "test: enforce native parity and safety"
```

## Checkpoint 21: Quality qualification

**Files:**
- Create: `docs/qualification/corpus/README.md`
- Create: `docs/qualification/corpus/preregistered-corpus.json`
- Create: `tools/qualification/CMakeLists.txt`
- Create: `tools/qualification/src/qualification_runner.cpp`
- Create: `tools/qualification/tests/qualification_runner_test.cpp`
- Create: `docs/qualification/device-matrix.md`
- Modify: `docs/qualification/conservative-v1-profile.md`
- Modify: `docs/superpowers/plans/2026-07-11-local-acr-master-roadmap.md`

**Interfaces:**
- Consumes: checked-in demo database/audio fixtures and native C ABI injected-PCM recognition.
- Produces: deterministic injected-corpus runner and versioned acceptance report schema.

- [x] Add failing tests for corpus manifest parsing: seeds, trigger IDs, positive/negative trial definitions, SNR/RT60/gain/offset fields, and holdout/calibration split validation.
- [x] Implement the manifest parser and runner dry-run mode that prints exact planned trial counts without needing real-device audio.
- [x] Add injected positive/negative fixture trials for the demo database and require deterministic JSONL result output.
- [x] Add latency/matched-position aggregation and threshold checks for the small checked-in fixture set.
- [x] Create `docs/qualification/device-matrix.md` with required Android/iPhone devices, room/distance/ambient fields, and evidence slots marked pending manual execution.
- [x] Update `docs/qualification/conservative-v1-profile.md` with the corpus manifest digest and checked-in fixture-run metrics.
- [x] Run native suites, Gradle regressions, iOS XCTest, and qualification runner tests.
- [x] Commit: `test: qualify Local ACR recognition profile`

## Checkpoint 22: Release packaging

**Files:**
- Create: `docs/release/notices/NOTICE.md`
- Create: `docs/release/sbom/local-acr-mvp-sbom.json`
- Create: `docs/release/provenance/local-acr-mvp-provenance.md`
- Create: `tools/release/package_local_acr.py`
- Create: `tools/release/tests/package_local_acr_test.py`
- Modify: `docs/release/local-acr-mvp-release-manifest.md`
- Modify: `docs/superpowers/plans/2026-07-11-local-acr-master-roadmap.md`

**Interfaces:**
- Consumes: checkpoint 21 qualified artifacts and existing build outputs.
- Produces: reproducible release staging directory with Android APK/AAR, iOS framework/app references, native CLI, `.lacrdb`, notices, SBOM, provenance, and checksums.

- [ ] Add failing packaging tests that require the release manifest to list every artifact, SHA-256 digest, source commit, build command, dependency notice, and exclusion proof for `examples/`.
- [ ] Implement `tools/release/package_local_acr.py` using explicit paths and no shell interpolation.
- [ ] Generate SBOM/provenance/NOTICE files from pinned dependency metadata and checked-in Local ACR files.
- [ ] Build Android, iOS simulator framework/app test target, native CLI, and demo database.
- [ ] Stage release artifacts under `build/release/local-acr-mvp/` and verify manifest digests.
- [ ] Run all checkpoint verification plus packaging tests.
- [ ] Commit: `build: package Local ACR MVP artifacts`

## Self-Review

- Spec coverage: checkpoint 20 covers Sections 11.1, 14.2, 14.5, 15 parity/safety/soak gates, and 21 profile-freeze prerequisites. Checkpoint 21 covers Sections 14.3, 14.4, 15 quality gates, and 21 matcher-calibration protocol. Checkpoint 22 covers Sections 16 licensing/privacy/provenance and release artifact packaging.
- Placeholder scan: no implementation step asks an engineer to invent unspecified behavior; checkpoint 21 real-device fields are explicitly evidence slots because physical device execution cannot be fabricated.
- Type consistency: checkpoint 20 consumes the existing native/KMP/iOS names and introduces only `LACRRecognizer` as the Swift façade surface referenced by the spec.
