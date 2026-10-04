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
