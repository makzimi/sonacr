# Sonacr

Sonacr recognizes songs on a phone from a few seconds of microphone audio. It works like Shazam, except that the fingerprint database ships inside the app and nothing goes to a server.

> **Work in progress.** The Android sample app works on a real phone. The APIs, the database format and the recognition thresholds can still change, and the iOS app does not recognize audio yet. Do not use this in production.

## Current working state

https://github.com/user-attachments/assets/cc972c2d-46cf-45d7-a311-68c8c5f0a895

## How it works

1. You give the database builder a list of tracks. It finds the strongest peaks in each track's spectrogram, pairs nearby peaks, and stores each pair as a 24-bit hash with its time in the track.
2. The app bundles that database. On the listening screen it reads the microphone and computes the same hashes for the last 4 seconds of audio.
3. The matcher looks up the hashes and counts how many of them line up at the same time offset in one track. A track wins when enough hashes agree and it clearly beats the runner-up for two evaluations in a row.
4. The app shows the title, the artist and the position in the song.

One C++ engine does all of this on Android, on iOS and in the desktop tools, so a database built on a Mac gives the same results on a phone. [docs/architecture.md](docs/architecture.md) describes the pipeline, the database format and the threading model in detail.

## What works today

Numbers from 5 test songs, measured on 2026-10-04:

| Test | Result |
|---|---|
| Desktop, 5-second clips from 8 places in each song, with and without added noise | 80 of 80 recognized, 0 wrong, median 2.8 s to a match |
| Pixel 7, 1 m from a MacBook speaker, 3 clips per song | 10 of 15 recognized, 0 wrong, matched about 2 to 6 s into the clip |
| 10 minutes of unrelated audio, on desktop and on the phone | 0 false matches |

The phone misses some clips because a room and a laptop speaker destroy most of the fine detail the fingerprints depend on. [docs/qualification/device-matrix.md](docs/qualification/device-matrix.md) has every trial and the conditions we did not measure.

Not done yet:

- The iOS app runs only in the simulator and does not deliver results to the UI.
- Recognition over the air needs to get more reliable.
- The SDK has no published package yet.

The full list of remaining work is in [docs/roadmap.md](docs/roadmap.md).

## Try it on Android

You need a Mac with:

- CMake 3.28 or newer, Ninja, Python 3, FFmpeg and JDK 17
- the Android SDK with NDK `27.2.12479018` and CMake `3.31.6`, both installable from Android Studio's SDK Manager
- an Android phone with Android 8.0 or newer and USB debugging enabled

Homebrew installs the first group with `brew install cmake ninja python ffmpeg openjdk@17`.

Put your own audio in `local-tracks/`. Git ignores this folder, so your music stays on your machine. Then add `local-tracks/manifest.json`:

```json
{
  "schemaVersion": 1,
  "databaseId": "my-tracks",
  "databaseVersion": "1",
  "triggers": [
    {"id": "first-song", "displayName": "First Song", "audio": "first-song.mp3", "metadata": {"artist": "Artist One"}},
    {"id": "second-song", "displayName": "Second Song", "audio": "second-song.mp3", "metadata": {"artist": "Artist Two"}}
  ]
}
```

The database builder accepts MP3 and WAV files, up to 100 tracks and 60 minutes of audio in total.

Build the tools, build the database, and install the app:

```bash
cmake --preset macos-clang-debug
cmake --build build/macos-clang-debug
python3 tools/demo/build_demo_assets.py
export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
./gradlew :androidApp:installDebug
```

Open "Local ACR" on the phone, allow the microphone, and play one of your songs nearby.

## Measure recognition on your computer

The benchmark plays clips of your tracks through the same engine the phone uses and counts correct, wrong and missed matches:

```bash
python3 tools/bench/recognition_bench.py \
  --manifest local-tracks/manifest.json \
  --db-tool build/macos-clang-debug/native/cli/local_acr_db \
  --probe build/macos-clang-debug/tools/probe/local_acr_probe \
  --positions 8 --noise 0,0.08
```

Add `--negative <file>` with audio that is not in your library to check for false matches.

Run the tests:

```bash
ctest --preset macos-clang-debug
./gradlew :shared:testDebugUnitTest :androidApp:testDebugUnitTest
```

## License

Copyright 2026 Maxim Kachinkin.

Sonacr is licensed under the [Apache License 2.0](LICENSE). Third-party code in `third_party/` keeps its own license. KISS FFT and SpeexDSP use BSD-3-Clause, and SQLite is in the public domain. Their notices are in `third_party/notices/`.
