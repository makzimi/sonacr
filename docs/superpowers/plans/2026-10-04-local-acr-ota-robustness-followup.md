# Local ACR Over-the-Air Robustness — Follow-up Research Plan

> Status: proposed. This is a research plan (experiments with measurable exit criteria), not yet an executable task plan. Expand the chosen direction with superpowers:writing-plans before implementing.

**Goal:** Raise on-device recognition at ~1 m from 10/15 to ≥ 14/15 clips within ~5 s, keeping 0 wrong IDs and 0 false callbacks.

**Starting point (2026-10-04, branch `feat/android-prototype`):** Pixel 7, MacBook speakers at volume 60, ~1 m: 10/15 recognized, 0 wrong, 0 false callbacks in 600 s (see `docs/qualification/device-matrix.md`). Desktop benchmark on the same 5 tracks: 80/80.

## What we know

- No on-device bug: replaying the phone's exact capture through the desktop probe reproduces the phone's behavior; no audio is dropped; native push averages 31 ms per 85 ms chunk (Debug build).
- Remaining misses are `InsufficientEvidence`: the correct track leads the runner-up, but only 3–7 exact landmark matches survive the acoustic path (minimum is 8). Clean audio of the same excerpt yields 35–58.
- Over the air the 4 s query saturates the 512-landmark cap: room noise and reverb generate many non-music peaks that compete for the 5 peaks/frame slots.
- Fan-out 6–10 raised aligned counts but (a) did not shorten time-to-first-match on its own and (b) at fan-out 6 the build-time ambiguity gate rejects the library (`g9-recyclable <-> g9-to-the-moon` share material).
- The coverage gate (0.015) is inactive at the query cap; false-positive safety rests on evidence/margin/runner-up-ratio/consecutive gates and has only 2 × 600 s of negative evidence.

## Evidence corpus (build first)

1. Capture ≥ 30 over-the-air recordings (5 tracks × 3 positions × 2 distances: 1 m, 3 m) with the diagnostic PCM dump (patch kept outside the repo; re-create as a debug-only build flag), store in git-ignored `local-tracks/ota/`.
2. Add `--ota-dir` to `tools/bench/recognition_bench.py` so OTA captures are scored as positives (first-hit time measured from playback start) alongside the synthetic-noise trials.
3. Record ≥ 1 hour of over-the-air negative audio (speech, unrelated music, room noise) through the phone for false-positive measurement.

Exit: desktop replay of the OTA corpus reproduces the device hit rate within ±1 clip.

## Experiments (one variable at a time, each scored on the OTA corpus + negatives + existing desktop bench)

| # | Hypothesis | Change | Keep if |
|---|---|---|---|
| E1 | Noise peaks crowd out music peaks | Spectral whitening / per-band normalization before peak picking; or raise `kMaxCandidatesPerFrame` while thresholding against a local-mean floor | OTA aligned counts ↑, desktop 80/80 kept |
| E2 | Exact `dt` breaks under ±1-frame timing jitter | Quantize `dt` (and/or bins) coarser in the hash for both DB and query; re-tune ambiguity gate | OTA first-hit ≤ 5 s on ≥ 14/15 |
| E3 | Query window too short once trimmed to 512 | Cap per-second landmark density (keep strongest per time slice) instead of keeping newest 512 | Effective window back to 4 s |
| E4 | Mic processing hurts music | Capture with `VOICE_RECOGNITION` or `UNPROCESSED` (pass a Context so `isUnprocessedSupported` works) | Device hit rate ↑ |
| E5 | Evidence gate can drop with separation-based acceptance | Replace inactive coverage gate with a winner/background significance test | 0 false callbacks on ≥ 1 h negatives |

Each kept change: TDD unit tests for the new behavior, regenerated goldens, bumped profile names (`landmark-v2` / `conservative-v2`) so stale databases are rejected, and a fresh device run recorded in `device-matrix.md`.

## Also in scope when touching the matcher

- Ambiguity gate redesign for songs: same-track secondary offsets (repeated choruses) should not block a confident track ID; cross-track ambiguity stays a build-time error.
- Release native build (`-O2`) for the Android library and a latency/CPU measurement on device.
