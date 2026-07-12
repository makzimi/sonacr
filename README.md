# Sonacr

> **WIP DRAFT:** Sonacr is under active development. APIs, database format details, build packaging, recognition thresholds, and sample applications may still change. Do not treat this repository as a production-ready SDK release yet.

Sonacr is a local sonic ACR project: an on-device audio fingerprinting and recognition stack for mobile apps. It builds local fingerprint databases from audio cues, bundles those databases into Android/iOS sample apps, listens through the microphone on a foreground screen, and triggers app callbacks when a known cue is recognized.

## What is included

- Portable C++20 native fingerprinting, database, matching, and C ABI layers.
- Kotlin Multiplatform shared SDK lifecycle layer.
- Android sample app with local promotion/check-in style reaction.
- iOS SwiftUI sample app with the same local database.
- Native CLI tooling for building, inspecting, and verifying `.lacrdb` fingerprint databases.
- Qualification and release-packaging scaffolding.

## Current status

The MVP roadmap is implemented as a sequence of local commits and verified test gates. This is still a draft repository: real-device acoustic qualification, public SDK polish, and distribution hardening should happen before any production usage.

## Privacy stance

Sonacr is designed for local recognition. Raw microphone audio is processed in bounded memory and is not uploaded, persisted, or logged by the SDK.

## Reference material

Legacy/reference projects used during design analysis are intentionally not committed or packaged. The local `examples/` directory, if present, is analysis-only.
