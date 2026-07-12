# MVP Device Matrix Evidence

Status: evidence slots are pending manual execution with physical devices. This file defines the required fields so results can be collected without changing the protocol.

## Required devices

| Platform | Device role | Device identifier | OS version | Status |
|---|---|---|---|---|
| Android | low/midrange | Pending manual selection | Pending | Pending checkpoint 21 manual run |
| Android | current | Pending manual selection | Pending | Pending checkpoint 21 manual run |
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
