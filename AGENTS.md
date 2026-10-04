# Agent instructions

Sonacr recognizes songs on a phone from a few seconds of microphone audio. A C++ engine builds and matches audio fingerprints, a Kotlin Multiplatform SDK wraps it, and Android and iOS sample apps use the SDK. Nothing goes to a server.

## Read first

- [docs/architecture.md](docs/architecture.md) has the design decisions, the fingerprint pipeline, the matcher gates, the database format and the threading model.
- [docs/roadmap.md](docs/roadmap.md) is the list of remaining work.
- [docs/qualification/](docs/qualification/) holds measured results. Never write a number there that you did not measure.

## Build and test

Requirements are macOS, CMake 3.28 or newer, Ninja, Python 3, FFmpeg, JDK 17, and the Android SDK with NDK `27.2.12479018` and CMake `3.31.6`.

```bash
# Native engine, CLI and tests
cmake --preset macos-clang-debug && cmake --build build/macos-clang-debug && ctest --preset macos-clang-debug
cmake --preset macos-asan && cmake --build build/macos-asan && ctest --preset macos-asan

# Kotlin SDK and Android app. Gradle 8.10 fails on newer JDKs with only the message "25.0.2".
export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
./gradlew :shared:testDebugUnitTest :shared:jvmTest :androidApp:testDebugUnitTest :androidApp:assembleDebug

# Release packaging tests
python3 -m unittest discover -s tools/release/tests -p '*_test.py'
```

If Gradle cannot find the Android SDK, create `local.properties` with `sdk.dir=<path to the SDK>`. Git ignores that file.

To measure recognition after any engine change, run `tools/bench/recognition_bench.py`. The exact command is in [docs/qualification/conservative-v1-profile.md](docs/qualification/conservative-v1-profile.md). Run it before and after the change and compare.

## Rules

Commits and attribution:

- The project owner is the only author. Do not add `Co-Authored-By` lines or any other AI attribution to commits or pull requests.
- This repository is public. Keep personal data out of files and commit messages, including home-folder paths, email addresses and device serial numbers. Use `$HOME`, `<device-serial>` and similar placeholders.

Audio:

- Use only audio from `local-tracks/` for tests, benchmarks and device runs. Do not use files from `examples/` or any other outside source. If a test needs audio that is not there, ask the owner to add it.
- Never commit anything from `local-tracks/`, or the generated `androidApp/src/main/assets/tracks.lacrdb` and `catalog.tsv`. Git ignores them, so do not force-add them.
- Before playing audio through the speakers, tell the owner which files will play and wait for a go-ahead.

Engine:

- Keep `-Werror` and the strict floating-point flags. Fix warnings instead of silencing them.
- Do not change `native/core/include/local_acr/local_acr.h` without raising `LACR_ABI_VERSION` and updating both bridges in `native/android` and `native/ios`.
- A change to fingerprint constants changes every stored hash. Regenerate the golden file with `./build/macos-clang-debug/native/tests/parity_goldens_test --write-golden native/tests/golden/parity_landmarks.json`, rebuild the demo database with `python3 tools/demo/build_demo_assets.py`, and update [docs/architecture.md](docs/architecture.md).
- Keep [docs/architecture.md](docs/architecture.md) in sync with the code. When a design decision changes, update it in the same commit.

## Documents

Put durable decisions in `docs/architecture.md` and remaining work in `docs/roadmap.md`. A working plan for a larger task can live in `docs/plans/` while the work is in progress. When the work is done, move anything that stays true into the architecture document, remove the finished items from the roadmap, and delete the plan.
