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


class ProbeFailure(RuntimeError):
    """Raised when the probe fails to complete or exits with an error."""
    pass


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


def probe_completed(text: str) -> bool:
    """Returns True if any stdout line parses as JSON with "event" == "end"."""
    for line in text.splitlines():
        try:
            record = json.loads(line)
            if record.get("event") == "end":
                return True
        except json.JSONDecodeError:
            pass
    return False


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
            "-i", f"anoisesrc=color=pink:amplitude={trial.noise}:sample_rate={PROBE_RATE}:seed=42",
            "-filter_complex",
            f"[0:a]aformat=channel_layouts=mono,aresample={PROBE_RATE}[a];[a][1:a]amix=inputs=2:normalize=0",
        ]
    command += ["-ac", "1", "-ar", str(PROBE_RATE), "-f", "s16le", str(output)]
    subprocess.run(command, check=True)


def run_trial(probe: Path, database: Path, trial: Trial, work_dir: Path) -> Outcome:
    raw = work_dir / "excerpt.s16"
    render_excerpt(trial, raw)
    result = subprocess.run([str(probe), str(database), str(raw), str(PROBE_RATE)], capture_output=True, text=True)
    if not probe_completed(result.stdout):
        raise ProbeFailure(f"probe failed (exit {result.returncode}) on {trial.source} @{trial.start_s}s: {result.stderr.strip()}")
    if result.returncode != 0:
        print(f"warning: probe exited {result.returncode}: {result.stderr.strip()}", file=sys.stderr)
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
        try:
            for trial in plan_trials(args.manifest, args):
                outcome = run_trial(args.probe, database, trial, work_dir)
                outcomes.append(outcome)
                label = trial.trigger_id or "NEGATIVE"
                print(f"{label:>24} @{trial.start_s:7.2f}s noise={trial.noise:<5} -> "
                      f"{outcome.recognized_id or '-'} first={outcome.first_hit_ms} callbacks={outcome.callbacks}")
        except ProbeFailure as e:
            print(str(e), file=sys.stderr)
            return 2

    summary = summarize(outcomes)
    print(json.dumps(summary, indent=2))
    if args.summary:
        args.summary.write_text(json.dumps(summary, indent=2) + "\n")
    passed = (summary["hitRate"] >= args.min_hit_rate and summary["wrong"] <= args.max_wrong
              and summary["falseCallbacks"] <= args.max_false_callbacks)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
