# MVP Device Matrix Evidence

Status: One Android device (Pixel 7, 1 m distance) executed on 2026-10-04; other rows pending manual execution with physical devices. This file defines the required fields so results can be collected without changing the protocol.

## Required devices

| Platform | Device role | Device identifier | OS version | Status |
|---|---|---|---|---|
| Android | low/midrange | Pending manual selection | Pending | Pending checkpoint 21 manual run |
| Android | current | Pixel 7 | Android 16 (BP4A.251205.006) | Run 2026-10-04 at 1 m: 10/15 recognized, 0 wrong, 0 false (see Results) |
| iOS | older supported iPhone | Pending manual selection | Pending | Pending checkpoint 21 manual run |
| iOS | current iPhone | Pending manual selection | Pending | Pending checkpoint 21 manual run |

## Required acoustic conditions

For each cue and each device:

- Distance: 1 meter and 3 meters.
- Ambient condition: two measured ranges between 55 and 70 dBA.
- Input SNR: at least 10 dB.
- Room RT60: representative 0.2 through 0.8 seconds.
- Positive trials: at least 10 per device/distance/ambient combination.
- Negative trials: at least 10 aggregate hours of unrelated acoustic input across the matrix with zero callbacks.

## Evidence record schema

Each manual run must record:

```json
{
  "deviceId": "human-readable device identifier",
  "platform": "android|ios",
  "osVersion": "exact OS build",
  "appCommit": "git commit hash",
  "databaseDigest": "lacrdb content digest",
  "distanceMeters": 1,
  "ambientDba": 60,
  "estimatedSnrDb": 15,
  "roomRt60Ms": 400,
  "cueId": "welcome-offer",
  "trials": 10,
  "recognized": 10,
  "falseCallbacks": 0,
  "medianLatencyMs": 0,
  "p95LatencyMs": 0,
  "maxMatchedPositionErrorMs": 0
}
```

## Results

### 2026-10-04 — Pixel 7, 1 m

**Setup:** MacBook Pro built-in speakers at macOS output volume 60, phone approximately 1 meter away, room (ambient level not measured).

**Conditions not measured:** Ambient dBA, SNR, and RT60 were not measured; 3 m distance was not run.

**Library:** 5 G9 tracks (2Mars, Ginger Wine, Recyclable, Say Da, To The Moon) with databaseDigest `9d9153b3bae5e68d3fab3699b8c1c39f4e903f1647d23f98b03788dddb9148c1`.

**Protocol:** For each track, 10-second clips starting at 0:30, 1:30, and 2:30, with the app restarted before each clip. Results read from the screen via adb (harness: `tools/bench/device_acoustic_test.py`). Then 600 seconds of unrelated audio (non-library music, speech-like clips, synthetic music) was played with the screen checked every ~3 seconds.

**Runs comparison**

| Parameter | Run 1 | Run 2 |
|---|---|---|
| App commit | e5c0a35 | f32a66d |
| Coverage gate | 0.05 | 0.015 |
| Recognized | 4/15 | 10/15 |
| Wrong title | 0 | 0 |
| False callbacks (600 s) | 0 | 0 |
| Latency (estimated from the matched position shown on the card, 1 s resolution) | 4–7 s | 2–6 s |
| Wall-clock, playback start to result visible on screen | not recorded | 4.5–9.1 s (median 6.8 s) |

**Per-trial results (Run 2)**

| Track | Clip start | Recognized | Seconds into clip |
|---|---|---|---|
| 2Mars | 0:30 | 2Mars | 3 |
| 2Mars | 1:30 | 2Mars | 4 |
| 2Mars | 2:30 | 2Mars | 2 |
| Ginger Wine | 0:30 | Ginger Wine | 4 |
| Ginger Wine | 1:30 | — | — |
| Ginger Wine | 2:30 | — | — |
| Recyclable | 0:30 | Recyclable | 6 |
| Recyclable | 1:30 | Recyclable | 4 |
| Recyclable | 2:30 | Recyclable | 3 |
| Say Da | 0:30 | — | — |
| Say Da | 1:30 | — | — |
| Say Da | 2:30 | Say Da | 5 |
| To The Moon | 0:30 | To The Moon | 4 |
| To The Moon | 1:30 | — | — |
| To The Moon | 2:30 | To The Moon | 2 |

**Evidence records**

```json
[
  {
    "deviceId": "Pixel 7",
    "platform": "android",
    "osVersion": "Android 16 (BP4A.251205.006)",
    "appCommit": "f32a66d",
    "databaseDigest": "9d9153b3bae5e68d3fab3699b8c1c39f4e903f1647d23f98b03788dddb9148c1",
    "distanceMeters": 1,
    "ambientDba": null,
    "estimatedSnrDb": null,
    "roomRt60Ms": null,
    "cueId": "g9-2mars",
    "trials": 3,
    "recognized": 3,
    "falseCallbacks": 0,
    "medianLatencyMs": 3000,
    "p95LatencyMs": 4000,
    "maxMatchedPositionErrorMs": null
  },
  {
    "deviceId": "Pixel 7",
    "platform": "android",
    "osVersion": "Android 16 (BP4A.251205.006)",
    "appCommit": "f32a66d",
    "databaseDigest": "9d9153b3bae5e68d3fab3699b8c1c39f4e903f1647d23f98b03788dddb9148c1",
    "distanceMeters": 1,
    "ambientDba": null,
    "estimatedSnrDb": null,
    "roomRt60Ms": null,
    "cueId": "g9-ginger-wine",
    "trials": 3,
    "recognized": 1,
    "falseCallbacks": 0,
    "medianLatencyMs": 4000,
    "p95LatencyMs": 4000,
    "maxMatchedPositionErrorMs": null
  },
  {
    "deviceId": "Pixel 7",
    "platform": "android",
    "osVersion": "Android 16 (BP4A.251205.006)",
    "appCommit": "f32a66d",
    "databaseDigest": "9d9153b3bae5e68d3fab3699b8c1c39f4e903f1647d23f98b03788dddb9148c1",
    "distanceMeters": 1,
    "ambientDba": null,
    "estimatedSnrDb": null,
    "roomRt60Ms": null,
    "cueId": "g9-recyclable",
    "trials": 3,
    "recognized": 3,
    "falseCallbacks": 0,
    "medianLatencyMs": 4000,
    "p95LatencyMs": 6000,
    "maxMatchedPositionErrorMs": null
  },
  {
    "deviceId": "Pixel 7",
    "platform": "android",
    "osVersion": "Android 16 (BP4A.251205.006)",
    "appCommit": "f32a66d",
    "databaseDigest": "9d9153b3bae5e68d3fab3699b8c1c39f4e903f1647d23f98b03788dddb9148c1",
    "distanceMeters": 1,
    "ambientDba": null,
    "estimatedSnrDb": null,
    "roomRt60Ms": null,
    "cueId": "g9-say-da",
    "trials": 3,
    "recognized": 1,
    "falseCallbacks": 0,
    "medianLatencyMs": 5000,
    "p95LatencyMs": 5000,
    "maxMatchedPositionErrorMs": null
  },
  {
    "deviceId": "Pixel 7",
    "platform": "android",
    "osVersion": "Android 16 (BP4A.251205.006)",
    "appCommit": "f32a66d",
    "databaseDigest": "9d9153b3bae5e68d3fab3699b8c1c39f4e903f1647d23f98b03788dddb9148c1",
    "distanceMeters": 1,
    "ambientDba": null,
    "estimatedSnrDb": null,
    "roomRt60Ms": null,
    "cueId": "g9-to-the-moon",
    "trials": 3,
    "recognized": 2,
    "falseCallbacks": 0,
    "medianLatencyMs": 3000,
    "p95LatencyMs": 4000,
    "maxMatchedPositionErrorMs": null
  }
]
```

**Known limitation:** Run 1 missed 11 of 15 clips because the coverage gate (aligned/query landmarks ≥ 0.05) rejected the correct track: over the air the 4 s query fills to its 512-landmark cap, while the correct track still led the runner-up 8–10×. Commit f32a66d lowered the gate to 0.015 (which at the 512-landmark query cap is inactive: any winner passing the evidence gate already has ratio >= 8/512 = 0.0156). Run 2 missed 5 of 15 clips: Ginger Wine 1:30, Ginger Wine 2:30, Say Da 0:30, Say Da 1:30, To The Moon 1:30. Desktop replays of phone captures show these are rejected for insufficient evidence — only 3–7 exact landmark matches survive the acoustic path against a minimum of 8. Improving fingerprint robustness to over-the-air audio is proposed as follow-up work.

**Latency note:** The "Seconds into clip" and latency figures above are estimated from the matched position shown on the card (1 s resolution). The measured wall-clock from playback start to the result being visible on screen was 4.5-9.1 s (median 6.8 s); this is an upper bound because it includes adb `uiautomator dump` polling (about 1-2 s per poll). Raw values (ms): 4485, 6587, 6670, 6737, 6770, 6847, 6849, 8940, 8970, 9051.
