# Conservative Matcher Profile `conservative-v1`

Status: implementation constants frozen for checkpoint 20 safety/parity gates; corpus qualification evidence is populated in checkpoint 21.

## Gate constants

| Gate | Value |
|---|---:|
| Minimum aligned landmarks | 12 |
| Minimum aligned ratio | 0.12 |
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

## Checkpoint 21 qualification evidence slots

These fields are intentionally pending until the preregistered corpus runner exists and is executed:

| Evidence | Status |
|---|---|
| Corpus manifest digest | Pending checkpoint 21 |
| Decoder/toolchain version | Pending checkpoint 21 |
| Positive injected holdout trials | Pending checkpoint 21 |
| Negative injected holdout duration | Pending checkpoint 21 |
| Median / p95 injected latency | Pending checkpoint 21 |
| Matched-position error distribution | Pending checkpoint 21 |
| Device matrix and real acoustic evidence | Pending checkpoint 21 |
