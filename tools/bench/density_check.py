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
