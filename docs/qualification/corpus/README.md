# Preregistered MVP Qualification Corpus

This directory defines the version-controlled corpus contract for checkpoint 21.

The checked-in manifest is intentionally small. It verifies the runner, metric aggregation, deterministic output, and evidence-recording flow without claiming full production accuracy. The larger release holdout described in the design remains a release gate to be populated with real recorded assets and device evidence before production distribution.

Rules:

- Calibration trials may be inspected while tuning a future matcher profile.
- Holdout trials must not be used for tuning after they are populated with real assets.
- Negative trials require `expectedCallbacks = 0`.
- Positive trials require `expectedCallbacks = 1` and a nonblank `triggerId`.
- Transform bounds follow the MVP moderate-condition envelope: positive SNR `10..25 dB`, RT60 `200..800 ms`, leading offset `0..2000 ms`, start phase `0..127` hops.
- The runner output is JSONL so every trial result and summary can be diffed and archived.
