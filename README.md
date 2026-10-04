# Sonacr

> **WIP DRAFT:** Sonacr is under active development. APIs, database format details, build packaging, recognition thresholds, and sample applications may still change. Do not treat this repository as a production-ready SDK release yet.

Sonacr is a local sonic ACR project: an on-device audio fingerprinting and recognition stack for mobile apps. It builds local fingerprint databases from audio cues, bundles those databases into Android/iOS sample apps, listens through the microphone on a foreground screen, and triggers app callbacks when a known cue is recognized.

## What is included

- Portable C++20 native fingerprinting, database, matching, and C ABI layers.
- Kotlin Multiplatform shared SDK lifecycle layer.
- Android sample app with local promotion/check-in style reaction.
- iOS SwiftUI sample app with the same local database.
- Native CLI tooling for building, inspecting, and verifying `.lacrdb` fingerprint databases.
- Desktop recognition benchmark (`tools/bench/recognition_bench.py`) and release-packaging scaffolding.

## Current status

The Android prototype recognizes bundled tracks on a real device (Pixel 7 evidence in `docs/qualification/device-matrix.md`: 10/15 clips recognized, 0 wrong; match position on the card 2-6 s into the clip, estimated from the matched position shown on the card at 1 s resolution; measured playback start to result visible on screen 4.5-9.1 s, median 6.8 s, an upper bound including adb polling) and shows a "now playing" screen. iOS is pending: it is simulator-only with no event delivery, and is covered by a follow-up plan (see `docs/superpowers/plans/2026-10-04-local-acr-android-prototype.md`). This is still a draft repository: broader device qualification, public SDK polish, and distribution hardening should happen before any production usage.

## Quick start (Android demo)

`local-tracks/` is git-ignored and must contain your own audio plus a `manifest.json` (format: see Task 5 of `docs/superpowers/plans/2026-10-04-local-acr-android-prototype.md`). Generated demo assets are also git-ignored.

```bash
cmake --preset macos-clang-debug && cmake --build build/macos-clang-debug
python3 tools/demo/build_demo_assets.py          # needs local-tracks/manifest.json
JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home ./gradlew :androidApp:installDebug
```

## Privacy stance

Sonacr is designed for local recognition. Raw microphone audio is processed in bounded memory and is not uploaded, persisted, or logged by the SDK.

## Reference material

Legacy/reference projects used during design analysis are intentionally not committed or packaged. The local `examples/` directory, if present, is analysis-only.
