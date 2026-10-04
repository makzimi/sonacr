# Local ACR Android Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Launch the Android sample on a real phone, play one of a few locally fingerprinted tracks from a nearby speaker, and see the app name the track within about five seconds — fully offline.

**Architecture:** Fix the never-cleared landmark de-duplication buffer, make query overflow non-fatal and retune the matcher gates, then fix the adaptive peak threshold that is far too sticky for real music — all measured by a new desktop benchmark that streams excerpts through the public C ABI. Then add the missing Android JNI library plus NDK build, deliver recognition events from the capture thread, and turn the demo into a "now playing" screen fed by a database built from the user's own tracks.

**Tech Stack:** C++20/C11 + CMake ≥ 3.28 (native core, CLI, probe), Python 3 stdlib + FFmpeg (benchmark tooling), Kotlin 2.0.21 Multiplatform + AGP 8.5.2 + Android NDK r27c (SDK), Jetpack Compose (demo), JDK 17 for Gradle.

## Why this plan exists (evidence, 2026-10-04)

The master roadmap marks all 22 checkpoints `committed`, but analysis showed the product does not work end to end. Every claim below was reproduced; the fix direction was validated in a scratch copy of the repo before this plan was written.

| Finding | Evidence |
|---|---|
| Real music yields ~0.3 landmarks/s (needs tens) | 4 real files (≈7 min) → 148 fingerprints; instrumented peak selector: average threshold 7–10 (ln units) vs. average decision value ≈ 0; only 50 candidates in 2,571 frames. Threshold decays `ln(0.997)` ≈ 0.26/s, so one loud onset blinds the selector for a minute. |
| `LandmarkBuilder` dies after 1,536 landmarks | `emitted_identities_` (capacity 512×3) is only cleared by `reset()` → `resource_limit_exceeded` → CLI prints `fingerprint generation failed`; a live session would die the same way. |
| Recognition of clean 5 s excerpts: 0 / 9 | Harness over the C ABI, even feeding the source audio digitally. |
| Query overflow kills the session | > 512 landmarks in the 4 s window emits terminal `QueryDensityExceeded`; every later push returns `INVALID_STATE`. Noise triggers this. |
| After fixing all of the above (decay `ln(0.934)`, identity expiry, non-fatal overflow, gates 8 / 0.05) | 4 real tracks, 5 positions × 3 noise levels: clean **20/20**, pink-noise 0.08 **16/20**, 0.15 **12/20**, **0 wrong IDs**, **0 false callbacks** on unrelated audio, median first-hit 2.8 s, p95 3.7 s. Synthetic set: clean 15/15, noisy 11/15, 0 wrong. All 32 native tests green. |
| Android cannot load the engine | Kotlin loads `local_acr_jni`, but no JNI C++ exists and Gradle has no NDK/CMake step; APK contains no engine `.so`. |
| Events never reach the app | `NativeSessionJni.pollOnce()` is only called from a test helper. |
| Demo shows promotions for one synthetic cue | `venue-demo.lacrdb` = 1 trigger / 74 fingerprints; asset copy never refreshes a stale file. |
| Qualification numbers are fabricated | `run_fixture_trials` sets `recognized = (kind == Positive)` and computes latency from a formula. |

Scope: engine fixes + desktop benchmark + Android prototype on the connected Pixel 7 (`33131FDH20039E`, Android 16, arm64-v8a). iOS is deliberately out of scope — see "Follow-up plan: iOS" at the end.

## Global Constraints

- Android `minSdk` 26, `compileSdk` 34; ABIs `arm64-v8a` (device) and `x86_64` (emulator).
- Android NDK `27.2.12479018`; Android-side CMake `3.31.6` (root `CMakeLists.txt` requires ≥ 3.28, newer than AGP's default 3.22.1).
- Gradle must run on JDK 17: `export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home` (the default Android Studio JBR is Java 25, which Gradle 8.10.2 rejects with the bare message `25.0.2`).
- Native code keeps the existing flags: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wsign-conversion -Werror`, strict FP. Never weaken `-Werror`; fix the warning.
- `ffmpeg` and `ffprobe` must be on `PATH` (the CLI decodes through them).
- Never commit copyrighted audio or databases built from it. User tracks live in `local-tracks/` (git-ignored); generated demo assets are git-ignored.
- Keep the C ABI (`native/core/include/local_acr/local_acr.h`) unchanged in this plan.
- Commit style: conventional prefixes (`feat:`, `fix:`, `test:`, `build:`, `docs:`), one commit per task, message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Native verification command set (referred to below as **native suite**):
  ```bash
  cmake --preset macos-clang-debug && cmake --build build/macos-clang-debug && ctest --preset macos-clang-debug
  cmake --preset macos-asan && cmake --build build/macos-asan && ctest --preset macos-asan
  ```
- Kotlin verification command (referred to below as **Kotlin suite**), with JDK 17 exported:
  ```bash
  ./gradlew :shared:testDebugUnitTest :shared:jvmTest :androidApp:testDebugUnitTest
  ```

## File map

| File | Responsibility | Task |
|---|---|---|
| `tools/bench/make_synthetic_track.py` | Deterministic music-like WAV generator (stdlib only) | 1 |
| `tools/probe/src/local_acr_probe.c`, `tools/probe/CMakeLists.txt` | Streams raw PCM through the public C ABI, prints JSON-lines events | 1 |
| `tools/bench/recognition_bench.py`, `tools/bench/tests/test_recognition_bench.py` | Builds DB, renders excerpts (+noise), scores hits/wrong/false/latency | 1 |
| `tools/bench/CMakeLists.txt`, `tools/bench/density_check.py` | CTest wiring; landmark-density regression gate | 1, 4 |
| `native/core/src/fingerprint/landmark_builder.cpp` | Expire de-duplication identities with their anchors | 2 |
| `tools/generate_gaussian_penalties_q16.py`, `native/core/src/fingerprint/gaussian_penalties_q16.hpp` | Faster threshold decay | 4 |
| `native/core/src/session/recognizer.cpp`, `native/core/src/matcher/matcher_profile.hpp` | Non-fatal query overflow; retuned gates | 3 |
| `native/android/src/local_acr_jni.cpp`, `native/android/CMakeLists.txt`, root `CMakeLists.txt`, `shared/build.gradle.kts` | JNI library and NDK build | 6 |
| `shared/src/androidMain/.../NativeSessionJni.kt`, `AudioRecordCapture.kt` | Poll decoding, event drain after push, error surfacing, no self-join | 6, 7 |
| `tools/demo/build_demo_assets.py`, `androidApp/.../{TrackCatalog,DemoController,MainActivity}.kt` | Track DB + catalog; "now playing" UI | 8 |
| `docs/qualification/device-matrix.md` | Real device evidence | 9 |
| `tools/qualification/**`, docs, README | Remove fabricated runner; correct status claims | 10 |

---

### Task 1: Desktop measurement tools (synthetic tracks, C ABI probe, benchmark)

Everything later is judged by these tools, so they come first and must work against the *unchanged* engine.

**Files:**
- Create: `tools/bench/make_synthetic_track.py`
- Create: `tools/probe/src/local_acr_probe.c`
- Create: `tools/probe/CMakeLists.txt`
- Create: `tools/bench/recognition_bench.py`
- Create: `tools/bench/tests/test_recognition_bench.py`
- Create: `tools/bench/CMakeLists.txt` (unit-test entry only; density check added in Task 4)
- Modify: `CMakeLists.txt` (root, host-only block)

**Interfaces:**
- Produces: `local_acr_probe <database.lacrdb> <pcm_s16le_mono.raw> <sample_rate>` → stdout JSON lines: `{"event":"recognized","triggerId":…,"confidence":…,"alignedCount":…,"atMs":…}`, `{"event":"sessionError","error":N,"atMs":…}`, `{"event":"pushFailed","status":N,"atMs":…}`, final `{"event":"end","durationMs":…}`; exit 0 unless a push fails.
- Produces: `recognition_bench.py --manifest M --db-tool T --probe P [--excerpt-seconds 5] [--positions 5] [--noise 0,0.08,0.15] [--negative FILE]… [--summary out.json] [--min-hit-rate R] [--max-wrong N] [--max-false-callbacks N]`; prints per-trial lines and a JSON summary with keys `positiveTrials, correct, wrong, hitRate, byNoise, medianLatencyMs, p95LatencyMs, negativeTrials, falseCallbacks`; exit 1 when a threshold is violated.
- Produces: `make_synthetic_track.py --seed N [--seconds S] out.wav` (22,050 Hz mono s16).

- [ ] **Step 1: Write the synthetic track generator**

`tools/bench/make_synthetic_track.py`:
```python
#!/usr/bin/env python3
"""Deterministic music-like test audio: chords, melody, and percussion (stdlib only)."""
import argparse
import math
import random
import struct
import wave

SAMPLE_RATE = 22050
SCALE = [0, 2, 3, 5, 7, 8, 10]  # natural minor


def note_hz(base_midi: int, degree: int) -> float:
    octave, step = divmod(degree, len(SCALE))
    midi = base_midi + 12 * octave + SCALE[step]
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def render(seed: int, seconds: float) -> list[float]:
    rng = random.Random(seed)
    total = int(seconds * SAMPLE_RATE)
    out = [0.0] * total
    beat = int(rng.uniform(0.18, 0.32) * SAMPLE_RATE)
    base = rng.randint(45, 57)
    for start in range(0, total, beat):
        length = min(beat * rng.choice([1, 1, 2]), total - start)
        voices = [note_hz(base, rng.randint(0, 6)), note_hz(base, rng.randint(7, 20))]
        if rng.random() < 0.5:
            voices.append(note_hz(base, rng.randint(14, 27)))
        for hz in voices:
            amp = rng.uniform(0.08, 0.18)
            for n in range(length):
                t = n / SAMPLE_RATE
                env = min(1.0, n / 200.0) * math.exp(-3.0 * t)
                s = math.sin(2 * math.pi * hz * t) + 0.5 * math.sin(4 * math.pi * hz * t) + 0.25 * math.sin(6 * math.pi * hz * t)
                out[start + n] += amp * env * s
        hit = min(int(0.04 * SAMPLE_RATE), total - start)
        drum = rng.uniform(0.1, 0.3)
        for n in range(hit):
            out[start + n] += drum * (rng.random() * 2 - 1) * math.exp(-n / (0.008 * SAMPLE_RATE))
    peak = max(1e-9, max(abs(v) for v in out))
    return [0.89 * v / peak for v in out]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("output")
    args = parser.parse_args()
    samples = render(args.seed, args.seconds)
    with wave.open(args.output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(b"".join(struct.pack("<h", int(v * 32767)) for v in samples))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Write the probe**

`tools/probe/src/local_acr_probe.c`:
```c
#include <local_acr/local_acr.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { kChunkFrames = 1024, kPayloadCapacity = 512 };

static unsigned long long frames_to_ms(uint64_t frames, uint32_t sample_rate) {
  return (unsigned long long)(frames * 1000u / sample_rate);
}

static void drain_events(lacr_recognizer_t *recognizer, uint64_t pushed_frames, uint32_t sample_rate) {
  for (;;) {
    lacr_event_t event;
    uint8_t payload[kPayloadCapacity];
    size_t required = 0;
    if (lacr_recognizer_poll_event(recognizer, &event, payload, sizeof payload, &required) != LACR_STATUS_OK) {
      return;
    }
    if (event.type == LACR_EVENT_RECOGNITION) {
      printf("{\"event\":\"recognized\",\"triggerId\":\"%.*s\",\"confidence\":%.3f,"
             "\"alignedCount\":%u,\"atMs\":%llu}\n",
             (int)event.trigger_id_length, (const char *)payload + event.trigger_id_offset,
             (double)event.confidence, event.aligned_count, frames_to_ms(pushed_frames, sample_rate));
    } else {
      printf("{\"event\":\"sessionError\",\"error\":%d,\"atMs\":%llu}\n", (int)event.error,
             frames_to_ms(pushed_frames, sample_rate));
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: local_acr_probe <database.lacrdb> <pcm_s16le_mono.raw> <sample_rate>\n");
    return 2;
  }
  const long rate_arg = strtol(argv[3], NULL, 10);
  if (rate_arg <= 0 || rate_arg > 192000) {
    fprintf(stderr, "invalid sample rate\n");
    return 2;
  }
  const uint32_t sample_rate = (uint32_t)rate_arg;
  FILE *input = fopen(argv[2], "rb");
  if (input == NULL) {
    fprintf(stderr, "cannot open pcm input\n");
    return 2;
  }

  char error_text[256] = {0};
  lacr_error_buffer_t error = {error_text, sizeof error_text, 0};
  const lacr_config_t config = {LACR_ABI_VERSION, sample_rate};
  lacr_recognizer_t *recognizer = NULL;
  if (lacr_recognizer_create(argv[1], &config, &recognizer, &error) != LACR_STATUS_OK ||
      lacr_recognizer_prepare(recognizer, &error) != LACR_STATUS_OK ||
      lacr_recognizer_start_session(recognizer, 1, &error) != LACR_STATUS_OK) {
    fprintf(stderr, "recognizer setup failed: %s\n", error_text);
    fclose(input);
    lacr_recognizer_destroy(recognizer);
    return 1;
  }

  int16_t chunk[kChunkFrames];
  uint64_t pushed = 0;
  int exit_code = 0;
  for (;;) {
    const size_t frames = fread(chunk, sizeof chunk[0], kChunkFrames, input);
    if (frames == 0) {
      break;
    }
    const void *planes[1] = {chunk};
    const lacr_pcm_view_t view = {planes, 1, (uint32_t)frames, 1, sample_rate, pushed, LACR_S16_INTERLEAVED};
    const lacr_status_t status = lacr_recognizer_push_pcm(recognizer, 1, &view);
    pushed += frames;
    drain_events(recognizer, pushed, sample_rate);
    if (status != LACR_STATUS_OK) {
      printf("{\"event\":\"pushFailed\",\"status\":%d,\"atMs\":%llu}\n", (int)status,
             frames_to_ms(pushed, sample_rate));
      exit_code = 1;
      break;
    }
  }
  printf("{\"event\":\"end\",\"durationMs\":%llu}\n", frames_to_ms(pushed, sample_rate));
  fclose(input);
  lacr_recognizer_stop_session(recognizer, 1, &error);
  lacr_recognizer_destroy(recognizer);
  return exit_code;
}
```

`tools/probe/CMakeLists.txt` (mirrors how `native/tests` builds the C ABI test `c_api_test.c`):
```cmake
add_executable(local_acr_probe src/local_acr_probe.c)
target_link_libraries(local_acr_probe PRIVATE local_acr_core)
target_compile_features(local_acr_probe PRIVATE c_std_11)
lacr_apply_sanitizers(local_acr_probe)
```

- [ ] **Step 3: Write the failing benchmark unit tests**

`tools/bench/tests/test_recognition_bench.py`:
```python
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from recognition_bench import Outcome, Trial, excerpt_starts, parse_probe_output, summarize  # noqa: E402


def outcome(trigger_id, recognized_id, first_ms=None, callbacks=0, noise=0.0):
    trial = Trial(trigger_id, Path("a.wav"), 0.0, 5.0, noise)
    return Outcome(trial, recognized_id, first_ms, callbacks)


class RecognitionBenchTest(unittest.TestCase):
    def test_excerpt_starts_are_centered_in_equal_slices(self):
        self.assertEqual(excerpt_starts(25.0, 5.0, 4), [2.5, 7.5, 12.5, 17.5])

    def test_excerpt_starts_clamp_short_audio_to_zero(self):
        self.assertEqual(excerpt_starts(3.0, 5.0, 2), [0.0, 0.0])

    def test_parse_probe_output_keeps_first_hit_and_counts_callbacks(self):
        text = "\n".join([
            '{"event":"recognized","triggerId":"a","confidence":0.9,"alignedCount":12,"atMs":3100}',
            '{"event":"recognized","triggerId":"b","confidence":0.8,"alignedCount":9,"atMs":3400}',
            '{"event":"end","durationMs":5000}',
        ])
        self.assertEqual(parse_probe_output(text), ("a", 3100, 2))

    def test_parse_probe_output_without_hits(self):
        self.assertEqual(parse_probe_output('{"event":"end","durationMs":5000}'), (None, None, 0))

    def test_summarize_scores_correct_wrong_missed_and_false_callbacks(self):
        summary = summarize([
            outcome("a", "a", first_ms=3000),
            outcome("a", "b", first_ms=2000),
            outcome("b", None),
            outcome("b", "b", first_ms=4000, noise=0.1),
            outcome(None, "a", callbacks=2),
        ])
        self.assertEqual(summary["positiveTrials"], 4)
        self.assertEqual(summary["correct"], 2)
        self.assertEqual(summary["wrong"], 1)
        self.assertEqual(summary["hitRate"], 0.5)
        self.assertEqual(summary["byNoise"]["0.0"], {"trials": 3, "correct": 1, "hitRate": 0.333})
        self.assertEqual(summary["medianLatencyMs"], 3500)
        self.assertEqual(summary["falseCallbacks"], 2)


if __name__ == "__main__":
    unittest.main()
```

Run: `python3 -m unittest discover -s tools/bench/tests -p 'test_*.py'`
Expected: FAIL — `ModuleNotFoundError: No module named 'recognition_bench'`.

- [ ] **Step 4: Write the benchmark**

`tools/bench/recognition_bench.py`:
```python
#!/usr/bin/env python3
"""Desktop recognition benchmark.

Builds a .lacrdb from a manifest, cuts excerpts from every trigger's audio (optionally mixed
with pink noise), streams each excerpt through the public C ABI via local_acr_probe at 48 kHz,
and scores the results. Negative files must produce zero callbacks.
"""
from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

PROBE_RATE = 48_000


@dataclass(frozen=True)
class Trial:
    trigger_id: str | None  # None marks a negative trial.
    source: Path
    start_s: float
    length_s: float
    noise: float


@dataclass(frozen=True)
class Outcome:
    trial: Trial
    recognized_id: str | None
    first_hit_ms: int | None
    callbacks: int


def excerpt_starts(duration_s: float, excerpt_s: float, count: int) -> list[float]:
    usable = max(0.0, duration_s - excerpt_s)
    return [round(usable * (index + 0.5) / count, 3) for index in range(count)]


def parse_probe_output(text: str) -> tuple[str | None, int | None, int]:
    first_id: str | None = None
    first_ms: int | None = None
    callbacks = 0
    for line in text.splitlines():
        record = json.loads(line)
        if record.get("event") != "recognized":
            continue
        callbacks += 1
        if first_id is None:
            first_id = record["triggerId"]
            first_ms = int(record["atMs"])
    return first_id, first_ms, callbacks


def summarize(outcomes: list[Outcome]) -> dict:
    positives = [o for o in outcomes if o.trial.trigger_id is not None]
    negatives = [o for o in outcomes if o.trial.trigger_id is None]
    correct = [o for o in positives if o.recognized_id == o.trial.trigger_id]
    wrong = [o for o in positives if o.recognized_id not in (None, o.trial.trigger_id)]
    by_noise: dict[str, dict] = {}
    for noise in sorted({o.trial.noise for o in positives}):
        group = [o for o in positives if o.trial.noise == noise]
        hits = [o for o in group if o.recognized_id == o.trial.trigger_id]
        by_noise[str(noise)] = {
            "trials": len(group),
            "correct": len(hits),
            "hitRate": round(len(hits) / len(group), 3),
        }
    latencies = sorted(o.first_hit_ms for o in correct if o.first_hit_ms is not None)
    return {
        "positiveTrials": len(positives),
        "correct": len(correct),
        "wrong": len(wrong),
        "hitRate": round(len(correct) / len(positives), 3) if positives else 0.0,
        "byNoise": by_noise,
        "medianLatencyMs": int(statistics.median(latencies)) if latencies else None,
        "p95LatencyMs": latencies[min(len(latencies) - 1, int(0.95 * len(latencies)))] if latencies else None,
        "negativeTrials": len(negatives),
        "falseCallbacks": sum(o.callbacks for o in negatives),
    }


def media_duration_s(path: Path) -> float:
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(path)],
        check=True, capture_output=True, text=True,
    )
    return float(result.stdout.strip())


def render_excerpt(trial: Trial, output: Path) -> None:
    command = ["ffmpeg", "-nostdin", "-v", "error", "-y", "-ss", str(trial.start_s), "-t", str(trial.length_s),
               "-i", str(trial.source)]
    if trial.noise > 0:
        command += [
            "-f", "lavfi", "-t", str(trial.length_s),
            "-i", f"anoisesrc=color=pink:amplitude={trial.noise}:sample_rate={PROBE_RATE}",
            "-filter_complex",
            f"[0:a]aformat=channel_layouts=mono,aresample={PROBE_RATE}[a];[a][1:a]amix=inputs=2:normalize=0",
        ]
    command += ["-ac", "1", "-ar", str(PROBE_RATE), "-f", "s16le", str(output)]
    subprocess.run(command, check=True)


def run_trial(probe: Path, database: Path, trial: Trial, work_dir: Path) -> Outcome:
    raw = work_dir / "excerpt.s16"
    render_excerpt(trial, raw)
    result = subprocess.run([str(probe), str(database), str(raw), str(PROBE_RATE)], capture_output=True, text=True)
    recognized_id, first_ms, callbacks = parse_probe_output(result.stdout)
    return Outcome(trial, recognized_id, first_ms, callbacks)


def plan_trials(manifest: Path, args: argparse.Namespace) -> list[Trial]:
    data = json.loads(manifest.read_text())
    trials: list[Trial] = []
    for trigger in data["triggers"]:
        source = (manifest.parent / trigger["audio"]).resolve()
        for start in excerpt_starts(media_duration_s(source), args.excerpt_seconds, args.positions):
            for noise in args.noise:
                trials.append(Trial(trigger["id"], source, start, args.excerpt_seconds, noise))
    for negative in args.negative:
        length = min(args.negative_seconds, media_duration_s(negative))
        trials.append(Trial(None, negative.resolve(), 0.0, length, 0.0))
    return trials


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--db-tool", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--excerpt-seconds", type=float, default=5.0)
    parser.add_argument("--positions", type=int, default=5)
    parser.add_argument("--noise", type=lambda text: [float(v) for v in text.split(",")], default=[0.0, 0.08, 0.15])
    parser.add_argument("--negative", type=Path, action="append", default=[])
    parser.add_argument("--negative-seconds", type=float, default=120.0)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--min-hit-rate", type=float, default=0.0)
    parser.add_argument("--max-wrong", type=int, default=0)
    parser.add_argument("--max-false-callbacks", type=int, default=0)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp:
        work_dir = Path(temp)
        database = work_dir / "bench.lacrdb"
        build = subprocess.run([str(args.db_tool), "build", str(args.manifest), str(database)],
                               capture_output=True, text=True)
        if build.returncode != 0:
            print(f"database build failed: {build.stderr.strip()}", file=sys.stderr)
            return 1
        print(f"database: {build.stdout.strip()}")
        outcomes = []
        for trial in plan_trials(args.manifest, args):
            outcome = run_trial(args.probe, database, trial, work_dir)
            outcomes.append(outcome)
            label = trial.trigger_id or "NEGATIVE"
            print(f"{label:>24} @{trial.start_s:7.2f}s noise={trial.noise:<5} -> "
                  f"{outcome.recognized_id or '-'} first={outcome.first_hit_ms} callbacks={outcome.callbacks}")

    summary = summarize(outcomes)
    print(json.dumps(summary, indent=2))
    if args.summary:
        args.summary.write_text(json.dumps(summary, indent=2) + "\n")
    passed = (summary["hitRate"] >= args.min_hit_rate and summary["wrong"] <= args.max_wrong
              and summary["falseCallbacks"] <= args.max_false_callbacks)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
```

Run: `python3 -m unittest discover -s tools/bench/tests -p 'test_*.py'`
Expected: `Ran 5 tests … OK`.

- [ ] **Step 5: Wire CMake**

`tools/bench/CMakeLists.txt` (density check is added in Task 4):
```cmake
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME recognition_bench_unit_test
         COMMAND Python3::Interpreter -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_*.py")
set_tests_properties(recognition_bench_unit_test PROPERTIES LABELS tools-unit)
```

In root `CMakeLists.txt`, extend the host-only block:
```cmake
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
  add_subdirectory(native/cli)
  add_subdirectory(tools/qualification)
  add_subdirectory(tools/probe)
  add_subdirectory(tools/bench)
endif()
```

- [ ] **Step 6: Build and record the baseline**

```bash
cmake --preset macos-clang-debug && cmake --build build/macos-clang-debug && ctest --preset macos-clang-debug
mkdir -p build/bench-synth && for s in 1 2 3; do python3 tools/bench/make_synthetic_track.py --seed $s build/bench-synth/synth-$s.wav; done
cat > build/bench-synth/manifest.json <<'EOF'
{"schemaVersion":1,"databaseId":"synth","databaseVersion":"1","triggers":[
{"id":"synth-1","displayName":"Synth 1","audio":"synth-1.wav","metadata":{}},
{"id":"synth-2","displayName":"Synth 2","audio":"synth-2.wav","metadata":{}},
{"id":"synth-3","displayName":"Synth 3","audio":"synth-3.wav","metadata":{}}]}
EOF
python3 tools/bench/recognition_bench.py --manifest build/bench-synth/manifest.json \
  --db-tool build/macos-clang-debug/native/cli/local_acr_db --probe build/macos-clang-debug/tools/probe/local_acr_probe \
  --noise 0,0.08 --summary build/bench-synth/baseline.json
```
Expected: CTest 31/31 pass. Benchmark either prints `database build failed: fingerprint generation failed` (identity-cap bug, Task 2) or a summary with `hitRate` near `0.0`. Both confirm the baseline is broken; note which one you saw in the commit message body.

- [ ] **Step 7: Commit**

```bash
git add tools/bench tools/probe CMakeLists.txt
git commit -m "test: add desktop recognition benchmark and C ABI probe"
```

---

### Task 2: Stop the landmark de-duplication buffer from filling up

**Files:**
- Modify: `native/core/src/fingerprint/landmark_builder.cpp` (`LandmarkBuilder::expire_before`)
- Test: `native/tests/unit/landmark_builder_test.cpp`

**Interfaces:**
- Consumes/Produces: `LandmarkBuilder::process(std::span<const ConfirmedPeak>, LandmarkSink&)` — unchanged signature; must keep returning ok for unbounded streams.

- [ ] **Step 1: Write the failing test**

Add above `int main()` in `native/tests/unit/landmark_builder_test.cpp`:
```cpp
void long_streams_do_not_exhaust_duplicate_tracking() {
  LandmarkBuilder builder;
  CapturingLandmarkSink sink;
  bool all_ok = true;
  for (std::uint32_t time = 0; time < 2000U; ++time) {
    const std::vector<ConfirmedPeak> frame_peaks{peak(time, 10), peak(time, 20)};
    all_ok = builder.process(frame_peaks, sink).ok() && all_ok;
  }
  check(all_ok, "two peaks per frame for 2000 frames process without resource exhaustion");
  check(sink.landmarks.size() > 1536U, "emission continues past the duplicate-tracking capacity");
}
```
and call it first in `main()`:
```cpp
int main() {
  long_streams_do_not_exhaust_duplicate_tracking();
  hash_layout_boundaries_and_rejection_rules();
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build/macos-clang-debug --target landmark_builder_test && ./build/macos-clang-debug/native/tests/landmark_builder_test`
Expected:
```
FAIL: two peaks per frame for 2000 frames process without resource exhaustion
FAIL: emission continues past the duplicate-tracking capacity
```

- [ ] **Step 3: Implement — expire identities together with their anchors**

An identity is `(hash, anchor_time_frame)`; once the anchor is older than `kMaximumDeltaTime` it can never be emitted again, so it can be forgotten. In `LandmarkBuilder::expire_before`, after `retained_count_ = write;` add:
```cpp
  std::size_t identity_write = 0;
  for (std::size_t read = 0; read < emitted_identity_count_; ++read) {
    const Landmark& identity = emitted_identities_[read];
    if (target_time_frame <= identity.anchor_time_frame ||
        target_time_frame - identity.anchor_time_frame <= kMaximumDeltaTime) {
      emitted_identities_[identity_write++] = identity;
    }
  }
  emitted_identity_count_ = identity_write;
```
Capacity stays sufficient: at most 5 peaks/frame × 97 live frames × 3 targets < 512 × 3.

- [ ] **Step 4: Run tests**

Run: `cmake --build build/macos-clang-debug --target landmark_builder_test && ./build/macos-clang-debug/native/tests/landmark_builder_test && echo OK`
Expected: `OK`, then run the **native suite** — all pass (the duplicate-identity test `duplicate_identity_retains_first_occurrence` must still pass).

- [ ] **Step 5: Commit**

```bash
git add native/core/src/fingerprint/landmark_builder.cpp native/tests/unit/landmark_builder_test.cpp
git commit -m "fix: expire landmark identities with their anchors"
```

---

### Task 3: Keep sessions alive under dense queries and retune matcher gates

**Files:**
- Modify: `native/core/src/session/recognizer.cpp` (`evaluate_until_current`, `evaluate_once`)
- Modify: `native/core/src/matcher/matcher_profile.hpp` (`kConservativeMatcherProfile`)
- Test: `native/tests/integration/recognizer_session_test.cpp`, `native/tests/unit/conservative_matcher_test.cpp`
- Modify: `docs/qualification/conservative-v1-profile.md` (gate table)

**Interfaces:**
- Produces: query window capped to the newest 512 landmarks without emitting `QueryDensityExceeded`; lookup resource limits skip one evaluation instead of ending the session.
- Produces: `minimum_aligned_landmarks = 8`, `minimum_aligned_ratio = 0.05` (other gates unchanged).

- [ ] **Step 1: Rewrite the density test to the new contract (fails first)**

In `recognizer_session_test.cpp`, replace the whole `query_density_error_is_terminal_and_resets_session()` function with:
```cpp
void query_density_overflow_keeps_session_alive() {
  const std::vector<float> samples = make_pcm(11025 * 3);
  local_acr::DatabaseReader reader = build_reader_from_pcm(samples);
  local_acr::Recognizer recognizer(11025);
  check_status_ok(recognizer.prepare(reader), "density recognizer prepares");
  check_status_ok(recognizer.start_session(9), "density session starts");
  check_status_ok(recognizer.inject_query_landmarks_for_test(513, 200),
                  "query overflow is trimmed instead of failing");

  const std::vector<local_acr::RecognizerEvent> events = drain(recognizer);
  for (const local_acr::RecognizerEvent& event : events) {
    check(event.type != local_acr::RecognizerEventType::SessionError, "overflow emits no session error");
  }
  check(recognizer.active(), "overflow keeps the session active");
}
```
and in `main()` replace the call with `query_density_overflow_keeps_session_alive();`.

In `conservative_matcher_test.cpp`, replace the whole `evidence_and_coverage_boundaries()` function with:
```cpp
void evidence_and_coverage_boundaries() {
  local_acr::ConservativeMatcher matcher;

  auto result = matcher.evaluate(evaluation({candidate("winner", 10, 7, 100)}));
  check(!result.recognized.has_value(), "aligned count 7 rejects");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::InsufficientEvidence,
        "aligned count diagnostic");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 12, 241)}, local_acr::PreviousWinner{
                                                                            .trigger_id = "winner",
                                                                            .center_bucket = 10,
                                                                        },
                                      241));
  check(!result.recognized.has_value(), "ratio below 5 percent rejects");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::InsufficientCoverage,
        "coverage diagnostic");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 8, 160)}, local_acr::PreviousWinner{
                                                                           .trigger_id = "winner",
                                                                           .center_bucket = 10,
                                                                       },
                                      160));
  check(result.recognized.has_value(), "aligned count 8 and ratio 5 percent accept");
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build/macos-clang-debug && ./build/macos-clang-debug/native/tests/recognizer_session_test; ./build/macos-clang-debug/native/tests/conservative_matcher_test`
Expected: `FAIL: query overflow is trimmed instead of failing`, `FAIL: overflow keeps the session active`, `FAIL: aligned count 8 and ratio 5 percent accept`.

- [ ] **Step 3: Make overflow non-fatal**

In `Recognizer::evaluate_until_current()` replace
```cpp
    if (query_.size() > kMaxQueryLandmarks) {
      emit_error(RecognizerError::QueryDensityExceeded);
      reset_session_state();
      active_ = false;
      return Status::resource_limit_exceeded();
    }
```
with
```cpp
    if (query_.size() > kMaxQueryLandmarks) {
      query_.erase(query_.begin(),
                   query_.end() - static_cast<std::ptrdiff_t>(kMaxQueryLandmarks));
    }
```
In `Recognizer::evaluate_once()` replace
```cpp
  if (!lookup_result.status.ok()) {
    if (lookup_result.status.code() == StatusCode::ResourceLimitExceeded) {
      emit_error(RecognizerError::QueryDensityExceeded);
      reset_session_state();
      active_ = false;
    }
    return lookup_result.status;
  }
```
with
```cpp
  if (!lookup_result.status.ok()) {
    if (lookup_result.status.code() == StatusCode::ResourceLimitExceeded) {
      return Status::ok_status();
    }
    return lookup_result.status;
  }
```
(`query_` is appended in time order, so erasing from the front drops the oldest landmarks.)

- [ ] **Step 4: Retune gates**

In `native/core/src/matcher/matcher_profile.hpp`, inside `kConservativeMatcherProfile`:
```cpp
    .minimum_aligned_landmarks = 8,
    .minimum_aligned_ratio = 0.05,
```
Update the two matching rows of the gate table in `docs/qualification/conservative-v1-profile.md` (`Minimum aligned landmarks | 8`, `Minimum aligned ratio | 0.05`).

- [ ] **Step 5: Run tests**

Run the **native suite**. Expected: 31/31 (Debug) and 31/31 (ASan) pass. Do not expect better benchmark numbers yet — landmark density is still the bottleneck (Task 4). This task exists first because Task 4's denser landmarks would otherwise trip the terminal overflow error (verified: `nominal_soak_test` fails if the order is reversed).

- [ ] **Step 6: Commit**

```bash
git add native/core/src/session/recognizer.cpp native/core/src/matcher/matcher_profile.hpp native/tests docs/qualification/conservative-v1-profile.md
git commit -m "fix: keep sessions alive under dense queries and retune gates"
```

---

### Task 4: Raise landmark density on real audio (threshold decay)

**Files:**
- Create: `tools/bench/density_check.py`
- Modify: `tools/bench/CMakeLists.txt`
- Modify: `tools/generate_gaussian_penalties_q16.py`
- Regenerate: `native/core/src/fingerprint/gaussian_penalties_q16.hpp`
- Modify: `native/tests/unit/peak_selector_test.cpp`, `native/tests/golden/peak_vectors.json`
- Regenerate: `native/tests/golden/parity_landmarks.json`

**Interfaces:**
- Produces: `kThresholdDecayQ16 == -4475` (`round_q16(ln(0.934))`); generator flag `--decay-base` (default `0.934`).
- Produces: CTest `landmark_density_check` (label `quality`) — fails below 20 landmarks/s on the synthetic set.

- [ ] **Step 1: Write the failing density gate**

`tools/bench/density_check.py`:
```python
#!/usr/bin/env python3
"""Fails when landmark density on deterministic music-like audio drops below a floor."""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

SEEDS = (1, 2, 3)
SECONDS = 30


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--db-tool", type=Path, required=True)
    parser.add_argument("--min-per-second", type=float, default=20.0)
    args = parser.parse_args()
    generator = Path(__file__).resolve().parent / "make_synthetic_track.py"

    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        triggers = []
        for seed in SEEDS:
            audio = work / f"synth-{seed}.wav"
            subprocess.run([sys.executable, str(generator), "--seed", str(seed), "--seconds", str(SECONDS), str(audio)],
                           check=True)
            triggers.append({"id": f"synth-{seed}", "displayName": f"Synth {seed}", "audio": audio.name,
                             "metadata": {}})
        manifest = work / "manifest.json"
        manifest.write_text(json.dumps({"schemaVersion": 1, "databaseId": "density", "databaseVersion": "1",
                                        "triggers": triggers}))
        database = work / "density.lacrdb"
        build = subprocess.run([str(args.db_tool), "build", str(manifest), str(database)],
                               capture_output=True, text=True)
        if build.returncode != 0:
            print(f"FAIL: database build failed: {build.stderr.strip()}")
            return 1
        fingerprints = json.loads(build.stdout)["fingerprints"]

    per_second = fingerprints / (len(SEEDS) * SECONDS)
    print(f"landmarks per second: {per_second:.1f} (floor {args.min_per_second})")
    return 0 if per_second >= args.min_per_second else 1


if __name__ == "__main__":
    raise SystemExit(main())
```

Append to `tools/bench/CMakeLists.txt`:
```cmake
add_test(NAME landmark_density_check
         COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/density_check.py"
                 --db-tool $<TARGET_FILE:local_acr_db>)
set_tests_properties(landmark_density_check PROPERTIES LABELS quality)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --preset macos-clang-debug && ctest --preset macos-clang-debug -R landmark_density_check --output-on-failure`
Expected: FAIL with `landmarks per second: 0.7 (floor 20.0)`.

- [ ] **Step 3: Parameterize the decay in the generator**

In `tools/generate_gaussian_penalties_q16.py`, add the flag before `output`:
```python
    parser = argparse.ArgumentParser()
    parser.add_argument("--decay-base", default="0.934")
    parser.add_argument("output")
```
and replace `decay = round_q16(Decimal("0.997").ln())` with:
```python
    decay = round_q16(Decimal(args.decay_base).ln())
```

- [ ] **Step 4: Regenerate the header**

Run: `python3 tools/generate_gaussian_penalties_q16.py native/core/src/fingerprint/gaussian_penalties_q16.hpp`
Expected stdout: `decay=-4475 self=0 far=-1048576 near=-1638`; `git diff --stat` shows only the `kThresholdDecayQ16` line changed in the header.

- [ ] **Step 5: Update the tests that pin the old constant**

`native/tests/unit/peak_selector_test.cpp`, in `generated_constants_are_checked()`:
```cpp
  check(local_acr::kThresholdDecayQ16 == reference_round_q16(std::log(0.934)),
        "threshold decay is round_q16(ln(0.934))");
```
`native/tests/golden/peak_vectors.json`: `"thresholdDecayQ16": -4475,`

Rebuild, then regenerate the parity golden (landmarks legitimately change with the profile):
```bash
cmake --build build/macos-clang-debug
./build/macos-clang-debug/native/tests/parity_goldens_test --write-golden native/tests/golden/parity_landmarks.json
```

- [ ] **Step 6: Run tests and the benchmark**

Run: `ctest --preset macos-clang-debug -R 'landmark_density_check|peak_selector_test|parity_goldens_test' --output-on-failure`
Expected: 3/3 pass; density prints `landmarks per second: 25.3 (floor 20.0)`.
Then run the **native suite** — Expected: 32/32 (Debug) and 32/32 (ASan).
Re-run the Task 1 Step 6 benchmark command with `--summary build/bench-synth/after.json --min-hit-rate 0.8 --max-wrong 0`.
Expected: exit 0; summary close to `"byNoise": {"0.0": {"hitRate": 1.0}, "0.08": {"hitRate": 0.733}}`, `"wrong": 0`.

- [ ] **Step 7: Commit**

```bash
git add tools/bench tools/generate_gaussian_penalties_q16.py native/core/src/fingerprint/gaussian_penalties_q16.hpp native/tests
git commit -m "fix: decay peak thresholds fast enough for real music"
```

---

### Task 5: Acceptance on your own tracks (desktop gate)

No production code. This decides whether the engine is good enough for *your* music before any mobile work. Do not skip it.

**Files:**
- Modify: `.gitignore` (add `local-tracks/`)
- Create (local only, ignored): `local-tracks/manifest.json` and 3–5 audio files

- [ ] **Step 1: Ignore local tracks**

Append to `.gitignore`:
```
# User-supplied tracks and assets derived from them. Never commit.
local-tracks/
androidApp/src/main/assets/tracks.lacrdb
androidApp/src/main/assets/catalog.tsv
```

- [ ] **Step 2: Add tracks and a manifest**

Copy 3–5 full tracks (MP3 or WAV, ≤ 100 MiB each, ≤ 60 min total) into `local-tracks/` and write `local-tracks/manifest.json`. Trigger IDs match `[A-Za-z0-9][A-Za-z0-9._-]{0,127}`; `displayName` is the title; `metadata.artist` is shown by the app:
```json
{
  "schemaVersion": 1,
  "databaseId": "my-tracks",
  "databaseVersion": "1",
  "triggers": [
    {"id": "track-1", "displayName": "First Song", "audio": "first-song.mp3", "metadata": {"artist": "Artist One"}},
    {"id": "track-2", "displayName": "Second Song", "audio": "second-song.mp3", "metadata": {"artist": "Artist Two"}},
    {"id": "track-3", "displayName": "Third Song", "audio": "third-song.mp3", "metadata": {"artist": "Artist Three"}}
  ]
}
```
Also put at least 10 minutes of unrelated audio (a podcast or music *not* in the manifest) at `local-tracks/negative.mp3`.

- [ ] **Step 3: Run the acceptance benchmark**

```bash
python3 tools/bench/recognition_bench.py --manifest local-tracks/manifest.json \
  --db-tool build/macos-clang-debug/native/cli/local_acr_db --probe build/macos-clang-debug/tools/probe/local_acr_probe \
  --positions 8 --noise 0,0.08 --negative local-tracks/negative.mp3 --negative-seconds 600 \
  --summary local-tracks/bench-summary.json --min-hit-rate 0.85 --max-wrong 0 --max-false-callbacks 0
```
Pass criteria: exit 0, `byNoise["0.0"].hitRate ≥ 0.95`, `medianLatencyMs ≤ 4000`.

- [ ] **Step 4: If it fails, tune in this order (one change per run, re-run Step 3 each time)**

| Symptom in summary | Change | Where |
|---|---|---|
| `database build failed: … ambiguous …` | Two tracks share material (intro/outro/sample). Remove one, or trim the shared section from one source file. | manifest / audio |
| Clean hit rate < 0.95, `wrong == 0` | Try `--decay-base 0.92`, regenerate header + parity golden as in Task 4 Steps 4–5. | generator |
| Noisy hit rate low, clean fine | Lower `minimum_aligned_ratio` to `0.04` and update the Task 3 test boundaries (`candidate(…, 12, 301)` rejects, `candidate(…, 8, 200)` accepts). | `matcher_profile.hpp` |
| `wrong > 0` or `falseCallbacks > 0` | Revert the last loosening; raise `minimum_aligned_landmarks` back to `10`. | `matcher_profile.hpp` |

Any constant change goes in its own `fix:` commit with the before/after summary JSON pasted in the body.

- [ ] **Step 5: Commit the ignore rule**

```bash
git add .gitignore
git commit -m "build: ignore local tracks and derived demo assets"
```

---

### Task 6: Android JNI library and NDK build

**Files:**
- Create: `native/android/src/local_acr_jni.cpp`
- Create: `native/android/CMakeLists.txt`
- Modify: root `CMakeLists.txt`
- Modify: `shared/build.gradle.kts`
- Modify: `shared/src/androidMain/kotlin/com/localacr/android/NativeSessionJni.kt` (poll ABI)
- Test: `shared/src/androidUnitTest/kotlin/com/localacr/android/NativeSessionJniTest.kt`

**Interfaces:**
- Produces JNI symbols (Kotlin `internal object JniNativeBridge`, instance methods): `Java_com_localacr_android_JniNativeBridge_{nativeCreate,nativePrepare,nativeStartSession,nativePushPcm,nativePollEvent,nativeStopSession,nativeDestroy}`.
- Changes Kotlin external: `nativePollEvent(handle: Long, slots: LongArray): String?` — returns the trigger ID for recognitions, else `null`; fills `slots` = `[status, type, error, matchedCueTimeFrame, confidence×1000, alignedCount]`.
- Produces: `internal fun decodePolledEvent(slots: LongArray, triggerId: String?): NativeBridgeEvent?`; `matchedPositionMs = frame × 128 × 1000 / 11025`.

- [ ] **Step 1: Install toolchain prerequisites**

```bash
export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
~/Library/Android/sdk/cmdline-tools/latest/bin/sdkmanager --list | grep -E '^ *(cmake;3\.3|ndk;27\.2)'
~/Library/Android/sdk/cmdline-tools/latest/bin/sdkmanager "ndk;27.2.12479018" "cmake;3.31.6"
ls ~/Library/Android/sdk/ndk/27.2.12479018/build/cmake/android.toolchain.cmake
```
Expected: the toolchain file exists. If `cmake;3.31.6` is not listed, install the newest listed `cmake;3.3x.y` (≥ 3.28) and use that version string in Step 5.

- [ ] **Step 2: Write the failing poll-decoding tests**

Add inside `class NativeSessionJniTest` in `NativeSessionJniTest.kt`:
```kotlin
    @Test
    fun decodesPolledRecognitionSlots() {
        val event = decodePolledEvent(longArrayOf(0, 1, 0, 861, 875, 14), "track-a")

        val recognition = event as NativeBridgeEvent.Recognition
        assertEquals("track-a", recognition.triggerId)
        assertEquals(0.875f, recognition.confidence)
        assertEquals(9_996L, recognition.matchedPositionMs)
    }

    @Test
    fun decodesNoEventAndSessionErrorSlots() {
        assertNull(decodePolledEvent(longArrayOf(7, 0, 0, 0, 0, 0), null))
        assertEquals(
            NativeBridgeEvent.SessionError(NativeBridgeStatus.ResourceLimitExceeded),
            decodePolledEvent(longArrayOf(0, 2, 1, 0, 0, 0), null),
        )
    }
```
Run: `./gradlew :shared:testDebugUnitTest`
Expected: compilation FAIL — `Unresolved reference 'decodePolledEvent'`.

- [ ] **Step 3: Implement Kotlin poll decoding**

In `NativeSessionJni.kt`, replace `override fun pollEvent(handle: Long): NativeBridgeEvent? = nativePollEvent(handle)` with:
```kotlin
    override fun pollEvent(handle: Long): NativeBridgeEvent? {
        val slots = LongArray(PollSlotCount)
        val triggerId = nativePollEvent(handle, slots)
        return decodePolledEvent(slots, triggerId)
    }
```
replace the external declaration with:
```kotlin
    private external fun nativePollEvent(handle: Long, slots: LongArray): String?
```
and insert directly above `private fun Int.toNativeBridgeStatus()`:
```kotlin
internal const val PollSlotCount = 6
private const val AnalysisHopFrames = 128L
private const val AnalysisSampleRate = 11_025L

/** Decodes the slot array filled by nativePollEvent: status, type, error, cue frame, confidence x1000, aligned count. */
internal fun decodePolledEvent(slots: LongArray, triggerId: String?): NativeBridgeEvent? {
    val status = slots[0].toInt().toNativeBridgeStatus()
    if (status == NativeBridgeStatus.NoEvent) {
        return null
    }
    if (status != NativeBridgeStatus.Ok) {
        return NativeBridgeEvent.SessionError(status)
    }
    if (slots[1] == 1L && triggerId != null) {
        return NativeBridgeEvent.Recognition(
            triggerId = triggerId,
            confidence = slots[4] / 1000f,
            matchedPositionMs = slots[3] * AnalysisHopFrames * 1000L / AnalysisSampleRate,
            resultAgeMs = 0L,
        )
    }
    return NativeBridgeEvent.SessionError(
        when (slots[2]) {
            1L -> NativeBridgeStatus.ResourceLimitExceeded
            2L -> NativeBridgeStatus.AudioDiscontinuity
            else -> NativeBridgeStatus.NativeEngineFailure
        },
    )
}
```
Run: `./gradlew :shared:testDebugUnitTest` — Expected: PASS.

- [ ] **Step 4: Write the JNI library**

`native/android/src/local_acr_jni.cpp` (checked against the project's `-Werror` flag set):
```cpp
#include <jni.h>

#include <local_acr/local_acr.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

// Slot layout shared with JniNativeBridge.decodePolledEvent in Kotlin.
constexpr jsize kPollSlotCount = 6;
constexpr std::size_t kPayloadCapacity = 512;

lacr_recognizer_t* from_handle(const jlong handle) noexcept {
  return reinterpret_cast<lacr_recognizer_t*>(static_cast<std::intptr_t>(handle));
}

std::size_t bytes_per_sample(const jint format) noexcept {
  return format == LACR_S16_INTERLEAVED ? sizeof(std::int16_t) : sizeof(float);
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL Java_com_localacr_android_JniNativeBridge_nativeCreate(
    JNIEnv* env, jobject /*thiz*/, jstring database_path, jint sample_rate) {
  if (database_path == nullptr || sample_rate <= 0) {
    return 0;
  }
  const char* path = env->GetStringUTFChars(database_path, nullptr);
  if (path == nullptr) {
    return 0;
  }
  const lacr_config_t config{LACR_ABI_VERSION, static_cast<std::uint32_t>(sample_rate)};
  lacr_recognizer_t* recognizer = nullptr;
  const lacr_status_t status = lacr_recognizer_create(path, &config, &recognizer, nullptr);
  env->ReleaseStringUTFChars(database_path, path);
  if (status != LACR_STATUS_OK) {
    return 0;
  }
  return static_cast<jlong>(reinterpret_cast<std::intptr_t>(recognizer));
}

extern "C" JNIEXPORT jint JNICALL Java_com_localacr_android_JniNativeBridge_nativePrepare(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
  return static_cast<jint>(lacr_recognizer_prepare(from_handle(handle), nullptr));
}

extern "C" JNIEXPORT jint JNICALL Java_com_localacr_android_JniNativeBridge_nativeStartSession(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jlong generation) {
  return static_cast<jint>(
      lacr_recognizer_start_session(from_handle(handle), static_cast<std::uint64_t>(generation), nullptr));
}

extern "C" JNIEXPORT jint JNICALL Java_com_localacr_android_JniNativeBridge_nativePushPcm(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jlong generation, jobject direct_buffer, jint frames,
    jint channels, jint sample_rate, jlong first_source_frame, jint format) {
  if (direct_buffer == nullptr || frames <= 0 || channels <= 0 || sample_rate <= 0 || first_source_frame < 0 ||
      format < LACR_S16_INTERLEAVED || format > LACR_F32_PLANAR) {
    return LACR_STATUS_INVALID_ARGUMENT;
  }
  void* data = env->GetDirectBufferAddress(direct_buffer);
  const jlong capacity = env->GetDirectBufferCapacity(direct_buffer);
  const std::size_t needed =
      static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels) * bytes_per_sample(format);
  if (data == nullptr || capacity < 0 || static_cast<std::size_t>(capacity) < needed) {
    return LACR_STATUS_INVALID_ARGUMENT;
  }
  const void* planes[1] = {data};
  const lacr_pcm_view_t view{
      planes,
      1U,
      static_cast<std::uint32_t>(frames),
      static_cast<std::uint32_t>(channels),
      static_cast<std::uint32_t>(sample_rate),
      static_cast<std::uint64_t>(first_source_frame),
      static_cast<lacr_sample_format_t>(format),
  };
  return static_cast<jint>(
      lacr_recognizer_push_pcm(from_handle(handle), static_cast<std::uint64_t>(generation), &view));
}

extern "C" JNIEXPORT jstring JNICALL Java_com_localacr_android_JniNativeBridge_nativePollEvent(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jlongArray out_slots) {
  if (out_slots == nullptr || env->GetArrayLength(out_slots) < kPollSlotCount) {
    return nullptr;
  }
  lacr_event_t event{};
  std::array<std::uint8_t, kPayloadCapacity> payload{};
  std::size_t required = 0;
  const lacr_status_t status =
      lacr_recognizer_poll_event(from_handle(handle), &event, payload.data(), payload.size(), &required);

  std::array<jlong, kPollSlotCount> slots{static_cast<jlong>(status), 0, 0, 0, 0, 0};
  if (status == LACR_STATUS_OK) {
    slots[1] = static_cast<jlong>(event.type);
    slots[2] = static_cast<jlong>(event.error);
    slots[3] = static_cast<jlong>(event.matched_cue_time_frame);
    slots[4] = static_cast<jlong>(std::lround(static_cast<double>(event.confidence) * 1000.0));
    slots[5] = static_cast<jlong>(event.aligned_count);
  }
  env->SetLongArrayRegion(out_slots, 0, kPollSlotCount, slots.data());

  if (status != LACR_STATUS_OK || event.type != LACR_EVENT_RECOGNITION) {
    return nullptr;
  }
  const std::string trigger_id(reinterpret_cast<const char*>(payload.data()) + event.trigger_id_offset,
                               event.trigger_id_length);
  return env->NewStringUTF(trigger_id.c_str());
}

extern "C" JNIEXPORT jint JNICALL Java_com_localacr_android_JniNativeBridge_nativeStopSession(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jlong generation) {
  return static_cast<jint>(
      lacr_recognizer_stop_session(from_handle(handle), static_cast<std::uint64_t>(generation), nullptr));
}

extern "C" JNIEXPORT void JNICALL Java_com_localacr_android_JniNativeBridge_nativeDestroy(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
  lacr_recognizer_destroy(from_handle(handle));
}
```

`native/android/CMakeLists.txt`:
```cmake
add_library(local_acr_jni SHARED src/local_acr_jni.cpp)
target_link_libraries(local_acr_jni PRIVATE local_acr_core)
target_compile_features(local_acr_jni PRIVATE cxx_std_20)
lacr_apply_warnings(local_acr_jni)
lacr_apply_strict_fp(local_acr_jni)
```

Root `CMakeLists.txt`: before `add_subdirectory(native/core)` add
```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
```
and after the `if(APPLE) … endif()` block add
```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  add_subdirectory(native/android)
endif()
```

- [ ] **Step 5: Wire the NDK build into the shared module**

Replace the `android { … }` block in `shared/build.gradle.kts` with:
```kotlin
android {
    namespace = "com.localacr.shared"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        minSdk = 26
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DLACR_BUILD_TESTS=OFF",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                )
                targets += "local_acr_jni"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.31.6"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
```

- [ ] **Step 6: Build and verify the library ships**

```bash
./gradlew :androidApp:assembleDebug
unzip -l androidApp/build/outputs/apk/debug/androidApp-debug.apk | grep liblocal_acr_jni
~/Library/Android/sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-nm -D --defined-only \
  shared/build/intermediates/cxx/Debug/*/obj/arm64-v8a/liblocal_acr_jni.so | grep -c ' T Java_'
```
Expected: `lib/arm64-v8a/liblocal_acr_jni.so` and `lib/x86_64/liblocal_acr_jni.so` listed; symbol count `7`.
If the NDK compiler reports a warning-as-error in core code, fix the warning in the named file (it is real on that target); never drop `-Werror`.

- [ ] **Step 7: Device smoke (old demo DB still bundled)**

```bash
adb -s 33131FDH20039E install -r androidApp/build/outputs/apk/debug/androidApp-debug.apk
adb -s 33131FDH20039E shell pm grant com.localacr.demo android.permission.RECORD_AUDIO
adb -s 33131FDH20039E shell am start -n com.localacr.demo/.MainActivity
adb -s 33131FDH20039E logcat -d -s AndroidRuntime:E | tail -20
```
Expected: screen shows `Status: Listening` (not `Recognition error: native recognizer create failed`); no `UnsatisfiedLinkError` in logcat.

- [ ] **Step 8: Run the Kotlin suite and commit**

Run the **Kotlin suite** — Expected: all pass.
```bash
git add native/android CMakeLists.txt shared/build.gradle.kts shared/src
git commit -m "feat: build Android JNI bridge for the native engine"
```

---

### Task 7: Deliver recognition events on Android

**Files:**
- Modify: `shared/src/androidMain/kotlin/com/localacr/android/NativeSessionJni.kt`
- Modify: `shared/src/androidMain/kotlin/com/localacr/android/AudioRecordCapture.kt`
- Test: `shared/src/androidUnitTest/kotlin/com/localacr/android/NativeSessionJniTest.kt`

**Interfaces:**
- Consumes: `NativeBridge.pollEvent(handle)` from Task 6.
- Produces: after every successful `pushPcm`, all queued native events (max 32) are delivered to the `onEvent` callback on the capture thread; a failed push is delivered as `NativeEvent.Error` *and* returned. `LocalAcrRecognizer` already re-dispatches to the main thread.

- [ ] **Step 1: Write the failing tests**

In `NativeSessionJniTest.kt`, add to `RecordingNativeBridge`:
```kotlin
    var pushStatus: NativeBridgeStatus = NativeBridgeStatus.Ok
```
change its `pushPcm` to `return pushStatus` instead of `return NativeBridgeStatus.Ok`, and add inside `class NativeSessionJniTest`:
```kotlin
    @Test
    fun pushPcmDeliversQueuedEventsWithoutExplicitPolling() {
        val bridge = RecordingNativeBridge()
        val session = NativeSessionJni("db.lacrdb", RecognitionConfig(), bridge)
        val events = mutableListOf<NativeEvent>()
        session.prepare()
        session.start { events += it }
        bridge.nextEvent = NativeBridgeEvent.Recognition("track-a", 0.9f, 2_000, 0)

        session.pushPcm(directPcm())

        val recognized = events.single() as NativeEvent.Recognized
        assertEquals("track-a", recognized.result.triggerId)
    }

    @Test
    fun failedPushIsReportedAsRuntimeEvent() {
        val bridge = RecordingNativeBridge()
        bridge.pushStatus = NativeBridgeStatus.InvalidState
        val session = NativeSessionJni("db.lacrdb", RecognitionConfig(), bridge)
        val events = mutableListOf<NativeEvent>()
        session.prepare()
        session.start { events += it }

        val error = session.pushPcm(directPcm())

        assertEquals(RecognitionErrorCode.InvalidState, error?.code)
        assertTrue(events.single() is NativeEvent.Error)
    }

    private fun directPcm(): PcmBuffer =
        PcmBuffer(
            buffer = ByteBuffer.allocateDirect(256).order(ByteOrder.nativeOrder()),
            frames = 64,
            channels = 1,
            sampleRate = 48_000,
            firstSourceFrame = 0,
            format = PcmBuffer.Format.S16Interleaved,
        )
```
Run: `./gradlew :shared:testDebugUnitTest`
Expected: FAIL — `pushPcmDeliversQueuedEventsWithoutExplicitPolling` (`List is empty`) and `failedPushIsReportedAsRuntimeEvent`.

- [ ] **Step 2: Drain events after each push and surface push failures**

In `NativeSessionJni.pushPcm`, replace the final `return bridge.pushPcm(…).toNullableError("native recognizer push failed")` with:
```kotlin
        val status = bridge.pushPcm(
            handle = handle,
            generation = generation,
            directBuffer = directBuffer,
            frames = pcm.frames,
            channels = pcm.channels,
            sampleRate = pcm.sampleRate,
            firstSourceFrame = pcm.firstSourceFrame,
            format = pcm.format,
        )
        if (status != NativeBridgeStatus.Ok) {
            val error = status.toRecognitionError("native recognizer push failed")
            onEvent?.invoke(NativeEvent.Error(error))
            return error
        }
        drainEvents()
        return null
```
Replace the start of `pollOnce` so it reports whether it consumed an event, and add the drain loop:
```kotlin
    private fun drainEvents() {
        repeat(MaxEventsPerDrain) {
            if (!pollOnce()) {
                return
            }
        }
    }

    private fun pollOnce(): Boolean {
        val event = bridge.pollEvent(handle) ?: return false
        val listener = onEvent ?: return true
```
end `pollOnce` with `return true` after the `when`, and add inside the class:
```kotlin
    private companion object {
        const val MaxEventsPerDrain = 32
    }
```
(`pollOnceForTest()` keeps calling `pollOnce()`; its Boolean result is ignored.)

- [ ] **Step 3: Avoid self-join when a runtime error stops capture from the capture thread**

`LocalAcrRecognizer.onRuntimeError` runs on the capture thread and calls `capture.stop()`. In `AudioRecordCapture.stop()` replace
```kotlin
        worker?.join(1_000)
```
with
```kotlin
        val current = worker
        if (current != null && current !== Thread.currentThread()) {
            current.join(1_000)
        }
```

- [ ] **Step 4: Run tests**

Run the **Kotlin suite**. Expected: shared 17 tests, app 8 tests, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add shared/src
git commit -m "fix: deliver native recognition events on Android"
```

---

### Task 8: "Now playing" demo with your tracks

**Files:**
- Create: `tools/demo/build_demo_assets.py`
- Create: `androidApp/src/main/kotlin/com/localacr/demo/TrackCatalog.kt`
- Replace: `androidApp/src/main/kotlin/com/localacr/demo/DemoController.kt`
- Modify: `androidApp/src/main/kotlin/com/localacr/demo/MainActivity.kt`
- Replace: `androidApp/src/test/kotlin/com/localacr/demo/DemoControllerTest.kt`
- Delete: `androidApp/src/main/assets/venue-demo.lacrdb`

**Interfaces:**
- Consumes: `local-tracks/manifest.json` (Task 5 format); `RecognitionResult.triggerId/confidence/matchedPositionMs`.
- Produces: assets `tracks.lacrdb` + `catalog.tsv` (`triggerId<TAB>title<TAB>artist` per line); `TrackCatalog.parse(tsv)`, `TrackCatalog.lookup(id): TrackInfo`; `DemoState.nowPlaying: NowPlaying?`; controller auto-restarts listening up to 3 times after runtime errors (counter resets on every recognition).

- [ ] **Step 1: Write the failing controller and catalog tests**

Replace `androidApp/src/test/kotlin/com/localacr/demo/DemoControllerTest.kt` with:
```kotlin
package com.localacr.demo

import com.localacr.RecognitionError
import com.localacr.RecognitionErrorCode
import com.localacr.RecognitionResult
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class DemoControllerTest {
    private val catalog = TrackCatalog(mapOf("track-a" to TrackInfo("Song A", "Artist A")))

    @Test
    fun waitsForPermissionBeforeListening() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)

        controller.onScreenVisible(permissionGranted = false)

        assertEquals(DemoListeningStatus.PermissionRequired, controller.state.status)
        assertFalse(sdk.started)
    }

    @Test
    fun preparesAndStartsWhenPermissionGranted() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)

        controller.onScreenVisible(permissionGranted = true)

        assertEquals(DemoListeningStatus.Listening, controller.state.status)
        assertTrue(sdk.prepared)
        assertTrue(sdk.started)
    }

    @Test
    fun recognitionShowsNowPlayingFromCatalog() {
        val states = mutableListOf<DemoState>()
        val controller = DemoController(FakeDemoSdk(), catalog, onStateChanged = { states += it })
        controller.onScreenVisible(permissionGranted = true)

        controller.onRecognized(result("track-a"))

        val nowPlaying = controller.state.nowPlaying
        assertEquals("Song A", nowPlaying?.title)
        assertEquals("Artist A", nowPlaying?.artist)
        assertEquals(83_000L, nowPlaying?.matchedPositionMs)
        assertEquals(nowPlaying, states.last().nowPlaying)
    }

    @Test
    fun unknownTriggerFallsBackToTriggerId() {
        val controller = DemoController(FakeDemoSdk(), catalog)
        controller.onScreenVisible(permissionGranted = true)

        controller.onRecognized(result("unlisted"))

        assertEquals("unlisted", controller.state.nowPlaying?.title)
    }

    @Test
    fun differentTrackReplacesNowPlaying() {
        val controller = DemoController(FakeDemoSdk(), catalog)
        controller.onScreenVisible(permissionGranted = true)

        controller.onRecognized(result("track-a"))
        controller.onRecognized(result("track-b"))

        assertEquals("track-b", controller.state.nowPlaying?.triggerId)
    }

    @Test
    fun screenDisappearStopsListening() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)
        controller.onScreenVisible(permissionGranted = true)

        controller.onScreenHidden()

        assertTrue(sdk.stopped)
        assertEquals(DemoListeningStatus.Idle, controller.state.status)
    }

    @Test
    fun runtimeErrorRestartsListeningAutomatically() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)
        controller.onScreenVisible(permissionGranted = true)

        controller.onError(error("transient"))

        assertEquals(2, sdk.startCount)
        assertEquals(DemoListeningStatus.Listening, controller.state.status)
        assertNull(controller.state.errorMessage)
    }

    @Test
    fun repeatedErrorsWithoutRecognitionAreDisplayed() {
        val sdk = FakeDemoSdk()
        val controller = DemoController(sdk, catalog)
        controller.onScreenVisible(permissionGranted = true)

        repeat(4) { controller.onError(error("broken")) }

        assertEquals(4, sdk.startCount)
        assertEquals(DemoListeningStatus.Error, controller.state.status)
        assertEquals("broken", controller.state.errorMessage)
    }

    private fun result(triggerId: String) =
        RecognitionResult(
            triggerId = triggerId,
            displayName = triggerId,
            confidence = 0.9f,
            matchedPositionMs = 83_000,
            resultAgeMs = 0,
            metadataJson = "{}",
        )

    private fun error(message: String) =
        RecognitionError(RecognitionErrorCode.NativeEngineFailure, message, recoverable = true)
}

class TrackCatalogTest {
    @Test
    fun parsesTabSeparatedLinesAndSkipsMalformedOnes() {
        val catalog = TrackCatalog.parse("a\tSong A\tArtist A\nb\tSong B\n\nbroken-line\n")

        assertEquals(2, catalog.size)
        assertEquals(TrackInfo("Song A", "Artist A"), catalog.lookup("a"))
        assertEquals(TrackInfo("Song B", ""), catalog.lookup("b"))
    }
}

private class FakeDemoSdk : DemoRecognizer {
    var prepared = false
    var started = false
    var stopped = false
    var startCount = 0

    override fun prepare(): DemoOperationResult {
        prepared = true
        return DemoOperationResult.Success
    }

    override fun start(listener: DemoRecognitionListener): DemoOperationResult {
        started = true
        startCount += 1
        return DemoOperationResult.Success
    }

    override fun stop(): DemoOperationResult {
        stopped = true
        return DemoOperationResult.Success
    }
}
```
Run: `./gradlew :androidApp:testDebugUnitTest`
Expected: compilation FAIL — `Unresolved reference 'TrackCatalog'`.

- [ ] **Step 2: Implement the catalog and controller**

`androidApp/src/main/kotlin/com/localacr/demo/TrackCatalog.kt`:
```kotlin
package com.localacr.demo

data class TrackInfo(
    val title: String,
    val artist: String,
)

/** Maps trigger IDs to display info. Source format: one `triggerId<TAB>title<TAB>artist` line per track. */
class TrackCatalog(private val tracks: Map<String, TrackInfo>) {
    val size: Int get() = tracks.size

    fun lookup(triggerId: String): TrackInfo = tracks[triggerId] ?: TrackInfo(title = triggerId, artist = "")

    companion object {
        fun parse(tsv: String): TrackCatalog =
            TrackCatalog(
                tsv.lineSequence()
                    .filter { it.isNotBlank() }
                    .map { it.split('\t') }
                    .filter { it.size >= 2 }
                    .associate { columns -> columns[0] to TrackInfo(columns[1], columns.getOrElse(2) { "" }) },
            )
    }
}
```

Replace `androidApp/src/main/kotlin/com/localacr/demo/DemoController.kt` with (the demo-level promotion cooldown is removed; the SDK's 30 s per-track cooldown still applies, and a different track replaces the card immediately):
```kotlin
package com.localacr.demo

import com.localacr.RecognitionError
import com.localacr.RecognitionResult

private const val MaxAutomaticRestarts = 3

enum class DemoListeningStatus {
    Idle,
    PermissionRequired,
    Preparing,
    Listening,
    Error,
}

data class NowPlaying(
    val triggerId: String,
    val title: String,
    val artist: String,
    val confidence: Float,
    val matchedPositionMs: Long,
)

data class DemoState(
    val status: DemoListeningStatus = DemoListeningStatus.Idle,
    val nowPlaying: NowPlaying? = null,
    val errorMessage: String? = null,
)

sealed class DemoOperationResult {
    data object Success : DemoOperationResult()
    data class Failure(val message: String) : DemoOperationResult()
}

interface DemoRecognitionListener {
    fun onRecognized(result: RecognitionResult)
    fun onError(error: RecognitionError)
}

interface DemoRecognizer {
    fun prepare(): DemoOperationResult
    fun start(listener: DemoRecognitionListener): DemoOperationResult
    fun stop(): DemoOperationResult
}

class DemoController(
    private val recognizer: DemoRecognizer,
    private val catalog: TrackCatalog = TrackCatalog(emptyMap()),
    initialState: DemoState = DemoState(),
    private val onStateChanged: (DemoState) -> Unit = {},
) : DemoRecognitionListener {
    var state: DemoState = initialState
        private set

    private var automaticRestarts = 0

    fun onScreenVisible(permissionGranted: Boolean) {
        if (!permissionGranted) {
            setState(state.copy(status = DemoListeningStatus.PermissionRequired, errorMessage = null))
            return
        }

        automaticRestarts = 0
        setState(state.copy(status = DemoListeningStatus.Preparing, errorMessage = null))
        when (val prepareResult = recognizer.prepare()) {
            DemoOperationResult.Success -> startListening()
            is DemoOperationResult.Failure -> showError(prepareResult.message)
        }
    }

    fun onScreenHidden() {
        recognizer.stop()
        setState(state.copy(status = DemoListeningStatus.Idle))
    }

    override fun onRecognized(result: RecognitionResult) {
        automaticRestarts = 0
        val track = catalog.lookup(result.triggerId)
        setState(
            state.copy(
                status = DemoListeningStatus.Listening,
                nowPlaying = NowPlaying(
                    triggerId = result.triggerId,
                    title = track.title,
                    artist = track.artist,
                    confidence = result.confidence,
                    matchedPositionMs = result.matchedPositionMs,
                ),
                errorMessage = null,
            ),
        )
    }

    override fun onError(error: RecognitionError) {
        if (state.status == DemoListeningStatus.Listening && automaticRestarts < MaxAutomaticRestarts) {
            automaticRestarts += 1
            startListening()
            return
        }
        showError(error.message)
    }

    private fun startListening() {
        when (val startResult = recognizer.start(this)) {
            DemoOperationResult.Success ->
                setState(state.copy(status = DemoListeningStatus.Listening, errorMessage = null))
            is DemoOperationResult.Failure -> showError(startResult.message)
        }
    }

    private fun showError(message: String) {
        setState(state.copy(status = DemoListeningStatus.Error, errorMessage = message))
    }

    private fun setState(nextState: DemoState) {
        state = nextState
        onStateChanged(nextState)
    }
}
```
Run: `./gradlew :androidApp:testDebugUnitTest` — Expected: 9 tests, 0 failures.

- [ ] **Step 3: Update the screen and asset handling**

Replace `androidApp/src/main/kotlin/com/localacr/demo/MainActivity.kt` with:
```kotlin
package com.localacr.demo

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import java.io.File

private const val DemoDatabaseAsset = "tracks.lacrdb"
private const val CatalogAsset = "catalog.tsv"

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    LocalAcrDemoApp()
                }
            }
        }
    }
}

@Composable
fun LocalAcrDemoApp() {
    val context = LocalContext.current
    val databasePath = remember {
        runCatching { context.copyAssetToFiles(DemoDatabaseAsset).absolutePath }.getOrNull()
    }
    if (databasePath == null) {
        Text(
            "Missing . Run tools/demo/build_demo_assets.py and rebuild the app.",
            modifier = Modifier.padding(24.dp),
        )
        return
    }
    var permissionGranted by remember {
        mutableStateOf(context.hasRecordAudioPermission())
    }
    var state by remember { mutableStateOf(DemoState()) }
    val controller = remember {
        val catalog = runCatching {
            TrackCatalog.parse(context.assets.open(CatalogAsset).bufferedReader().use { it.readText() })
        }.getOrDefault(TrackCatalog(emptyMap()))
        DemoController(
            SharedLocalAcrDemoRecognizer(databasePath),
            catalog,
            onStateChanged = { state = it },
        )
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        permissionGranted = granted
        controller.onScreenVisible(granted)
    }

    LaunchedEffect(permissionGranted) {
        controller.onScreenVisible(permissionGranted)
    }

    DisposableEffect(Unit) {
        onDispose {
            controller.onScreenHidden()
        }
    }

    DemoScreen(
        state = state,
        permissionGranted = permissionGranted,
        onRequestPermission = { permissionLauncher.launch(Manifest.permission.RECORD_AUDIO) },
    )
}

@Composable
fun DemoScreen(
    state: DemoState,
    permissionGranted: Boolean,
    onRequestPermission: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text("Local ACR", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(8.dp))
        Text("Play one of the bundled tracks nearby. Recognition runs fully on this device.")
        Spacer(Modifier.height(24.dp))
        Text("Status: ${state.status}")

        if (!permissionGranted) {
            Spacer(Modifier.height(16.dp))
            Button(onClick = onRequestPermission) {
                Text("Allow microphone")
            }
        }

        state.errorMessage?.let { message ->
            Spacer(Modifier.height(16.dp))
            Text("Recognition error: $message", color = MaterialTheme.colorScheme.error)
        }

        state.nowPlaying?.let { track ->
            Spacer(Modifier.height(24.dp))
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Now playing", style = MaterialTheme.typography.labelLarge)
                    Spacer(Modifier.height(8.dp))
                    Text(track.title, style = MaterialTheme.typography.titleLarge)
                    if (track.artist.isNotBlank()) {
                        Text(track.artist)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text("at ${formatPosition(track.matchedPositionMs)} · confidence ${(track.confidence * 100).toInt()}%")
                }
            }
        }
    }
}

private fun formatPosition(positionMs: Long): String {
    val totalSeconds = positionMs / 1_000
    return "%d:%02d".format(totalSeconds / 60, totalSeconds % 60)
}

private fun Context.hasRecordAudioPermission(): Boolean =
    ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

private fun Context.copyAssetToFiles(assetName: String): File {
    val target = File(filesDir, assetName)
    assets.open(assetName).use { input ->
        target.outputStream().use { output ->
            input.copyTo(output)
        }
    }
    return target
}
```
Note: `copyAssetToFiles` now always overwrites, so a rebuilt database is never shadowed by a stale copy.

- [ ] **Step 4: Asset builder**

`tools/demo/build_demo_assets.py`:
```python
#!/usr/bin/env python3
"""Builds the Android demo assets (tracks.lacrdb + catalog.tsv) from a track manifest."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def catalog_lines(manifest: dict) -> list[str]:
    lines = []
    for trigger in manifest["triggers"]:
        artist = str(trigger.get("metadata", {}).get("artist", ""))
        fields = [trigger["id"], trigger["displayName"], artist]
        if any("\t" in field or "\n" in field for field in fields):
            raise ValueError(f"tab or newline in catalog fields for {trigger['id']}")
        lines.append("\t".join(fields))
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=REPO_ROOT / "local-tracks/manifest.json")
    parser.add_argument("--db-tool", type=Path, default=REPO_ROOT / "build/macos-clang-debug/native/cli/local_acr_db")
    parser.add_argument("--out-dir", type=Path, default=REPO_ROOT / "androidApp/src/main/assets")
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text())
    args.out_dir.mkdir(parents=True, exist_ok=True)
    database = args.out_dir / "tracks.lacrdb"
    build = subprocess.run([str(args.db_tool), "build", str(args.manifest), str(database)],
                           capture_output=True, text=True)
    if build.returncode != 0:
        print(f"database build failed: {build.stderr.strip()}", file=sys.stderr)
        return 1
    (args.out_dir / "catalog.tsv").write_text("\n".join(catalog_lines(manifest)) + "\n")
    print(build.stdout.strip())
    print(f"wrote {database} and {args.out_dir / 'catalog.tsv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```
```bash
git rm androidApp/src/main/assets/venue-demo.lacrdb
python3 tools/demo/build_demo_assets.py
```
Expected: a JSON line with `"triggers":<your count>` and `"fingerprints":` in the thousands, then `wrote …/tracks.lacrdb and …/catalog.tsv`. `git status` must *not* list either generated file (ignored in Task 5).

- [ ] **Step 5: Build, test, commit**

```bash
./gradlew :androidApp:testDebugUnitTest :androidApp:assembleDebug
git add tools/demo androidApp/src
git commit -m "feat: show now-playing track in the Android demo"
```

---

### Task 9: On-device acoustic validation (Pixel 7)

**Files:**
- Modify: `docs/qualification/device-matrix.md` (replace "Pending" rows you executed with real values)

- [ ] **Step 1: Install and start**

```bash
./gradlew :androidApp:installDebug
adb -s 33131FDH20039E shell pm grant com.localacr.demo android.permission.RECORD_AUDIO
adb -s 33131FDH20039E shell am start -n com.localacr.demo/.MainActivity
```
Expected: `Status: Listening`.

- [ ] **Step 2: Positive trials**

Laptop speaker at ~1 m, normal volume, quiet room. For each track, play 10 s from three positions and time the card appearing:
```bash
ffplay -nodisp -autoexit -loglevel error -ss 30 -t 10 local-tracks/first-song.mp3
ffplay -nodisp -autoexit -loglevel error -ss 90 -t 10 local-tracks/first-song.mp3
ffplay -nodisp -autoexit -loglevel error -ss 150 -t 10 local-tracks/first-song.mp3
```
Between trials of the *same* track wait 30 s or tap away and back (SDK duplicate cooldown). Repeat at 3 m.
Pass: ≥ 8 of 9 trials per track at 1 m show the correct title within 5 s; no wrong title ever.

- [ ] **Step 3: Negative trial**

Play `local-tracks/negative.mp3` for 10 minutes at the same volume. Pass: no card appears.

- [ ] **Step 4: If it fails on device but passed Task 5**

1. `adb -s 33131FDH20039E logcat -d -s AndroidRuntime:E` — crashes first.
2. Try a different capture source: in `shared/src/androidMain/kotlin/com/localacr/android/AudioRecordCapture.kt` (`AndroidAudioRouting.preferredAudioSource`), switch to `MediaRecorder.AudioSource.VOICE_RECOGNITION` (less AGC/noise suppression than `MIC`), rebuild, repeat Step 2.
3. Reproduce the device conditions on desktop: record 10 s with the phone's mic (e.g., Recorder app), copy to the laptop, and run it through the probe:
   ```bash
   ffmpeg -v error -i phone-recording.m4a -ac 1 -ar 48000 -f s16le /tmp/rec.s16
   build/macos-clang-debug/tools/probe/local_acr_probe androidApp/src/main/assets/tracks.lacrdb /tmp/rec.s16 48000
   ```
   Tune with the Task 5 table using this recording as an extra bench input.

- [ ] **Step 5: Record evidence and commit**

Fill the Android "current" row and add one JSON evidence record per track/distance in `docs/qualification/device-matrix.md` using the existing schema (`deviceId: "Pixel 7"`, `osVersion` from `adb shell getprop ro.build.display.id`, `appCommit` from `git rev-parse HEAD`, `databaseDigest` from `local_acr_db inspect`).
```bash
git add docs/qualification/device-matrix.md
git commit -m "docs: record Pixel 7 acoustic recognition evidence"
```

---

### Task 10: Remove fabricated evidence and correct the docs

**Files:**
- Delete: `tools/qualification/` (runner, tests, CMake)
- Delete: `docs/qualification/corpus/`
- Modify: root `CMakeLists.txt` (drop `add_subdirectory(tools/qualification)`)
- Modify: `docs/superpowers/plans/2026-07-11-local-acr-master-roadmap.md`, `docs/qualification/conservative-v1-profile.md`, `README.md`

- [ ] **Step 1: Delete the fake runner**

```bash
git rm -r tools/qualification docs/qualification/corpus
```
Remove the `add_subdirectory(tools/qualification)` line from root `CMakeLists.txt`.
Run the **native suite** — Expected: all pass (one fewer test: `qualification_runner_test` is gone).

- [ ] **Step 2: Correct the docs**

- Roadmap: append a `## Status corrections (2026-10-04)` section stating that checkpoint 21's figures came from a runner that never executed the engine, that real-audio recognition and the Android JNI layer were missing until this plan, and link to this plan and to `docs/qualification/device-matrix.md`.
- `conservative-v1-profile.md`: replace the "Checkpoint 21 qualification evidence slots" table with a pointer to `tools/bench/recognition_bench.py` and the measured desktop + device numbers from Tasks 5 and 9.
- `README.md` "Current status": Android prototype recognizes bundled tracks on device; iOS pending (follow-up plan). Add a quick start:
  ```bash
  cmake --preset macos-clang-debug && cmake --build build/macos-clang-debug
  python3 tools/demo/build_demo_assets.py          # needs local-tracks/manifest.json
  JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home ./gradlew :androidApp:installDebug
  ```

- [ ] **Step 3: Commit**

```bash
git add -A CMakeLists.txt docs README.md
git commit -m "docs: replace fabricated qualification with measured evidence"
```

---

## Follow-up plan: iOS (not part of this plan)

Write a separate plan once Task 9 passes. Known gaps, verified during analysis:

1. **No device build** — Gradle declares only `iosSimulatorArm64`; add `iosArm64`, build `native/ios` for `iphoneos`, produce an XCFramework, point the Xcode project at it, set signing.
2. **No event delivery** — `IosLocalAcrPorts.pollOnce()` has no caller; drain after push like Task 7, or poll from a timer.
3. **DSP on the real-time thread** — the `AVAudioEngine` tap calls `lacr_recognizer_push_pcm`, which runs FFT, matching and SQLite under a mutex. Route the tap through the existing SPSC queue (`native/core/src/session/spsc_pcm_queue.*`) into a worker thread. (Android is unaffected: `AudioRecord` reads on an ordinary thread.)
4. **Stale bundled DB** — `iosApp/Resources/venue-demo.lacrdb` predates the new fingerprint profile; reuse `tools/demo/build_demo_assets.py` with an iOS output dir and port the now-playing UI to SwiftUI.

## Later hardening (after both prototypes work)

- `displayName`/`metadataJson` are replaced by the trigger ID / `{}` in both adapters because the C ABI event carries only the trigger ID; the demo uses `catalog.tsv` instead. Fix by adding trigger metadata to the ABI (version bump).
- `resultAgeMs` is always 0.
- Profile names `landmark-v1` / `conservative-v1` are now stored in databases whose fingerprints and gates changed; bump to `-v2` so old databases are rejected instead of silently mismatching.
- `LocalAcrRecognizer` mutates its state from the capture thread on runtime errors; confine it to the main dispatcher.
- CLI decodes with FFmpeg's resampler, the runtime uses SpeexDSP; the benchmark passes through both, but add a parity test if recognition drifts.
- The build-time ambiguity gate was designed for short cues; watch it with longer libraries (Task 5 table, first row).
