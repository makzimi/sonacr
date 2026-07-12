# Local ACR CLI and Database Builder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the local database command-line tool that validates manifests/audio inputs, creates `.lacrdb` files from decoded cue audio, verifies artifacts, and rejects ambiguous cue libraries.

**Architecture:** Add a separate `native/cli` C++ target named `local_acr_db` that reuses `local_acr_core` for fingerprinting, database writing/reading, and runtime matcher decisions. Keep manifest parsing, filesystem validation, decoder process invocation, command routing, and build/verify policies in focused files with unit/integration tests. Checkpoints 12–14 remain separate commits: input pipeline, database commands, ambiguity gate.

**Tech Stack:** C++20, CMake/CTest, POSIX process APIs on host builds, existing native core library, existing SQLite database writer/reader, no shell invocation for decoder tools.

## Global Constraints

- Manifest size is at most 1 MiB.
- Duplicate JSON object keys, invalid UTF-8, NUL bytes, and structural control characters are rejected.
- `databaseId`, `databaseVersion`, trigger `id`, `displayName`, and `audio` are required and nonblank.
- `databaseId`, `databaseVersion`, and trigger IDs use ASCII `[A-Za-z0-9][A-Za-z0-9._-]{0,127}`.
- A manifest contains at most 100 triggers and at most 60 aggregate minutes of decoded audio.
- Audio paths resolve relative to the manifest directory; canonical input path must remain beneath the canonical manifest directory.
- Only regular local WAV/PCM or MP3 files are accepted, each at most 100 MiB.
- Metadata must be a JSON object with canonical serialized size no greater than 16 KiB.
- Unknown top-level structural fields are rejected for schema version 1; arbitrary fields are allowed inside `metadata`.
- CLI exit categories: `0` success, `2` command/manifest/input validation, `3` audio decode failure, `4` filesystem/database output failure, `5` internal engine failure.
- `build` must never leave a partially valid output at the final path.
- `verify --release` must reject artifacts whose recorded decoder/builder toolchain does not match the frozen release profile.

---

## File Structure

- `native/cli/CMakeLists.txt`: builds `local_acr_db` and CLI test helpers.
- `native/cli/src/main.cpp`: thin command router.
- `native/cli/src/exit_code.hpp`: stable command exit categories.
- `native/cli/src/manifest.hpp/.cpp`: strict JSON parser, manifest validation, canonical audio path validation.
- `native/cli/src/audio_decoder.hpp/.cpp`: bounded FFprobe/FFmpeg argv construction and no-shell process execution for PCM streaming.
- `native/cli/src/database_commands.hpp/.cpp`: checkpoint 13 build/inspect/verify commands.
- `native/cli/src/ambiguity_gate.hpp/.cpp`: checkpoint 14 build-time ambiguity checks using runtime matcher.
- `native/cli/tests/manifest_test.cpp`: manifest/parser/path tests.
- `native/cli/tests/audio_decoder_test.cpp`: decoder argv/no-shell/bounds tests.
- `native/cli/tests/database_commands_test.cpp`: build/inspect/verify tests.
- `native/cli/tests/ambiguity_gate_test.cpp`: cross-trigger ambiguity rejection tests.

## Checkpoint 12: CLI input pipeline

**Files:**
- Create: `native/cli/CMakeLists.txt`
- Create: `native/cli/src/exit_code.hpp`
- Create: `native/cli/src/manifest.hpp`
- Create: `native/cli/src/manifest.cpp`
- Create: `native/cli/src/audio_decoder.hpp`
- Create: `native/cli/src/audio_decoder.cpp`
- Create: `native/cli/src/main.cpp`
- Create: `native/cli/tests/manifest_test.cpp`
- Create: `native/cli/tests/audio_decoder_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `local_acr::cli::load_manifest(path) -> ManifestResult`, `local_acr::cli::validate_audio_inputs(manifest) -> Status`, and `local_acr::cli::FfmpegDecoderPlan`.
- Later tasks consume: `ValidatedManifest`, `ManifestTrigger`, canonical audio paths, decoder argv vectors, and stable `ExitCode`.

- [x] **Step 1: Write failing manifest tests**

Test valid schema v1, duplicate keys, unknown top-level fields, missing required fields, invalid IDs, duplicate trigger IDs, metadata size, path traversal via symlink, unsupported extension, nonregular file, and 100 MiB input bound in `native/cli/tests/manifest_test.cpp`.

- [x] **Step 2: Write failing decoder tests**

Test FFprobe/FFmpeg argv is passed as vectors, not shell strings; suspicious filenames are preserved as a single argv element; decoded output caps are enforced; MP3/WAV extension admission is deterministic.

- [x] **Step 3: Run focused red tests**

Run: `cmake --build --preset macos-clang-debug --target manifest_test audio_decoder_test`

Expected: compile failure because `native/cli` and parser/decoder types are absent.

- [x] **Step 4: Implement strict manifest parser and path validator**

Implement a small schema-specific JSON parser with duplicate-key detection and canonical metadata serialization. Keep it deliberately narrow: objects, arrays, strings with JSON escapes, integers for `schemaVersion`, and metadata preservation as canonical JSON.

- [x] **Step 5: Implement no-shell decoder plan**

Implement FFprobe/FFmpeg command planning with explicit argv vectors and bounded output settings. Add a small process runner abstraction that can be tested with fake tools and does not invoke a shell.

- [x] **Step 6: Run focused and native suites**

Run: `ctest --preset macos-clang-debug --output-on-failure -R 'manifest_test|audio_decoder_test'`

Run: `ctest --preset macos-clang-debug --output-on-failure`

Expected: all tests pass.

- [x] **Step 7: Commit checkpoint 12**

Commit: `feat: decode validated CLI audio inputs`

## Checkpoint 13: Database commands

**Files:**
- Create: `native/cli/src/database_commands.hpp`
- Create: `native/cli/src/database_commands.cpp`
- Create: `native/cli/tests/database_commands_test.cpp`
- Modify: `native/cli/src/main.cpp`
- Modify: `native/cli/CMakeLists.txt`

**Interfaces:**
- Consumes: `ValidatedManifest`, decoder PCM stream, `Fingerprinter`, `DatabaseWriter`, `DatabaseReader`.
- Produces: `local-acr-db build`, `inspect`, `verify`, and `verify --release`.

- [x] **Step 1: Add failing command tests**

Test command validation, durable output replacement, build report metadata, inspect output, ordinary verify, release verify rejection for non-frozen toolchain, and stable exit categories.

- [x] **Step 2: Run focused red tests**

Expected: missing database command types and command handlers.

- [x] **Step 3: Implement build command**

Decode each input, validate audio duration/silence/clipping/landmark counts/density, fingerprint, write via `DatabaseWriter`, reopen via `DatabaseReader`, then atomically replace output.

- [x] **Step 4: Implement inspect/verify commands**

Inspect reads identity and summary. Verify uses `DatabaseReader`; release verification compares decoder/builder profile metadata.

- [x] **Step 5: Run command and sanitizer suites**

Run focused CLI tests, full Debug, ASan/UBSan, and TSan suites.

- [x] **Step 6: Commit checkpoint 13**

Commit: `feat: build and verify local ACR databases`

## Checkpoint 14: Library ambiguity gate

**Files:**
- Create: `native/cli/src/ambiguity_gate.hpp`
- Create: `native/cli/src/ambiguity_gate.cpp`
- Create: `native/cli/tests/ambiguity_gate_test.cpp`
- Modify: `native/cli/src/database_commands.cpp`
- Modify: `native/cli/CMakeLists.txt`

**Interfaces:**
- Consumes: per-trigger fingerprints/windows and `ConservativeMatcher`.
- Produces: build-time rejection for cue libraries where cross-trigger windows pass runtime gates.

- [x] **Step 1: Add failing ambiguity tests**

Test self matches are excluded, overlapping cross-trigger windows are checked every 255 ms, exact runtime gates are used, and ambiguous pairs produce trigger-specific diagnostics.

- [x] **Step 2: Run focused red tests**

Expected: missing ambiguity gate types.

- [x] **Step 3: Implement ambiguity prefilter and exact matcher pass**

Use a hash-intersection prefilter, then evaluate sliding two-to-four-second windows with the same candidate lookup/alignment/matcher semantics as runtime.

- [x] **Step 4: Integrate into build**

Reject ambiguous libraries before writing final output and store successful ambiguity summary in `build_report_json`.

- [x] **Step 5: Run complete CLI/native suites**

Run focused ambiguity tests, full Debug, ASan/UBSan, and TSan suites.

- [x] **Step 6: Commit checkpoint 14**

Commit: `feat: reject ambiguous cue libraries`

## Self-Review

- Spec coverage: Checkpoints 12–14 cover manifest rules, CLI command routing, audio validation, durable output, inspect/verify, release verification, and ambiguity rejection from design Sections 9.1–9.3.
- Placeholder scan: no `TBD`, `TODO`, or unowned implementation steps.
- Type consistency: checkpoint 12 produces `ValidatedManifest` and decoder plans consumed by checkpoint 13; checkpoint 14 consumes checkpoint 13 fingerprint/build data.
