# Conservative Matcher Profile `conservative-v1`

Status: implementation constants frozen for checkpoint 20 safety/parity gates; measured evidence is recorded below.

## Gate constants

| Gate | Value |
|---|---:|
| Minimum aligned landmarks | 8 |
| Minimum aligned ratio | 0.015 (inactive at the 512-landmark query cap; acceptance is governed by the evidence, margin, runner-up ratio, consecutive-winner and offset-stability gates) |
| Minimum winner margin over runner-up | 5 landmarks |
| Minimum winner / runner-up ratio | 1.25 |
| Required consecutive winner evaluations | 2 |
| Maximum offset bucket drift | 1 bucket |
| Ambiguous secondary offset ratio | 0.80 |

## Confidence formula

```text
evidence = min(1, aligned_count / 24)
coverage = min(1, aligned_ratio / 0.30)
separation = 1 when runner_up == 0, otherwise clamp(1 - runner_up / winner, 0, 1)
confidence = 0.45 * evidence + 0.35 * coverage + 0.20 * separation
```

Confidence is diagnostic. The gates above determine acceptance.

## Checkpoint 20 automated gates

- Serialized `landmark-v1` parity golden: `native/tests/golden/parity_landmarks.json`.
- Native parity CTest: `parity_goldens_test`.
- Malformed native input/property CTest: `malformed_input_property_test`.
- Nominal injected queue/recognizer soak CTest: `nominal_soak_test`.
- Swift façade contract XCTest: `LACRRecognizerContractTests`.
- Sanitizer release gates: host ASan/UBSan and TSan CTest presets.

## Measured recognition evidence

The checkpoint 21 "qualification runner" never executed the engine, so its figures were removed. Evidence now comes from `tools/bench/recognition_bench.py`, which streams excerpts through the C ABI via `tools/probe`.

Desktop benchmark on 5 user tracks (artist G9): 5 s excerpts at 8 positions per track, clean and with pink noise at amplitude 0.08, plus a 600 s negative of unrelated audio.

| Measure | Result |
|---|---|
| Correct, final engine | 80/80 (clean 40/40, noisy 40/40) |
| Wrong / false callbacks | 0 / 0 |
| Median / p95 first match | 2816 ms / 3754 ms |
| Baseline before engine fixes (synthetic set) | 0/30 recognized |

Engine fixes behind these numbers: landmark dedup buffer expiry, non-fatal query overflow, peak-threshold decay ln(0.934), and matcher gates (minimum aligned landmarks 8, minimum aligned ratio lowered from 0.05 to 0.015 after phone captures showed 0.05 rejected correct over-the-air matches; because the query is capped at 512 landmarks and at least 8 must align, any evidence-passing winner has ratio >= 8/512 = 0.0156, so the 0.015 gate is inactive at runtime and `InsufficientCoverage` is unreachable).

Exact desktop bench command that produced 80/80 (the script defaults differ: 5 positions and noise 0,0.08,0.15):

```bash
python3 tools/bench/recognition_bench.py --manifest local-tracks/manifest.json --db-tool build/macos-clang-debug/native/cli/local_acr_db --probe build/macos-clang-debug/tools/probe/local_acr_probe --positions 8 --noise 0,0.08 --negative local-tracks/negative.wav --negative-seconds 600 --min-hit-rate 0.85 --max-wrong 0 --max-false-callbacks 0
```

Device (Pixel 7, Android 16, about 1 m from MacBook speakers): run 2 recognized 10/15 clips, with the match position shown on the card at 2-6 s into the clip (estimated from the matched position shown on the card, 1 s resolution; measured wall-clock from playback start to result visible on screen was 4.5-9.1 s, median 6.8 s, an upper bound that includes adb `uiautomator dump` polling of about 1-2 s per poll), with 0 wrong and 0 false callbacks in 600 s of unrelated audio. Per-trial details are in `docs/qualification/device-matrix.md`.

Not yet measured: the 300-hour negative gate, a broader device matrix, and iOS.
