#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path
from typing import Iterable


BUILD_COMMANDS = [
    "cmake --build --preset macos-clang-debug",
    "ctest --preset macos-clang-debug --output-on-failure",
    "cmake --build --preset macos-asan && ctest --preset macos-asan --output-on-failure",
    "cmake --build --preset macos-tsan && ctest --preset macos-tsan --output-on-failure",
    "ANDROID_HOME=/Users/maxkach/Library/Android/sdk GRADLE_USER_HOME=/private/tmp/local-acr-gradle-home ./gradlew :shared:linkDebugFrameworkIosSimulatorArm64 :shared:jvmTest :shared:testDebugUnitTest :androidApp:testDebugUnitTest :androidApp:assembleDebug --rerun-tasks --no-daemon",
    "xcodebuild test -quiet -project iosApp/LocalAcrDemo.xcodeproj -scheme LocalAcrDemo -destination 'platform=iOS Simulator,id=<available-iPhone-simulator-id>' -derivedDataPath /private/tmp/local-acr-ios-derived CODE_SIGNING_ALLOWED=NO ARCHS=arm64 ONLY_ACTIVE_ARCH=YES",
    "python3 -m unittest tools/release/tests/package_local_acr_test.py",
]


class PackageResult:
    def __init__(self, output_dir: Path, manifest_path: Path) -> None:
        self.output_dir = output_dir
        self.manifest_path = manifest_path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_dependencies(repo_root: Path) -> list[dict]:
    lock_path = repo_root / "third_party" / "dependencies.lock.json"
    data = json.loads(lock_path.read_text())
    return list(data["dependencies"])


def copy_artifacts(output_dir: Path, artifacts: Iterable[Path]) -> list[dict]:
    artifact_dir = output_dir / "artifacts"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    entries: list[dict] = []
    for source in artifacts:
        if not source.exists() or not source.is_file():
            raise FileNotFoundError(f"artifact does not exist: {source}")
        if "examples" in source.parts:
            raise ValueError(f"examples/ is analysis-only and cannot be packaged: {source}")
        destination = artifact_dir / source.name
        shutil.copy2(source, destination)
        entries.append(
            {
                "path": str(destination.relative_to(output_dir)),
                "sha256": sha256_file(destination),
                "bytes": destination.stat().st_size,
            }
        )
    return entries


def write_notice(repo_root: Path, output_dir: Path, dependencies: list[dict]) -> Path:
    path = output_dir / "docs" / "release" / "notices" / "NOTICE.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Local ACR MVP Notices",
        "",
        "Local ACR production code is newly authored for this project.",
        "The legacy `examples/` directory is analysis-only and is not packaged.",
        "",
        "## Third-party dependencies",
        "",
    ]
    for dependency in dependencies:
        notice_path = repo_root / dependency["noticePath"]
        notice_excerpt = notice_path.read_text(errors="replace").splitlines()[0] if notice_path.exists() else ""
        lines.append(f"- {dependency['name']} {dependency['version']} — {dependency['spdxLicense']} — `{dependency['noticePath']}`")
        if notice_excerpt:
            lines.append(f"  - Notice begins: {notice_excerpt}")
    path.write_text("\n".join(lines) + "\n")
    return path


def write_sbom(output_dir: Path, dependencies: list[dict]) -> Path:
    path = output_dir / "docs" / "release" / "sbom" / "local-acr-mvp-sbom.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    sbom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "metadata": {"component": {"name": "local-acr-mvp", "type": "application"}},
        "components": [
            {
                "name": dependency["name"],
                "version": dependency["version"],
                "licenses": [{"license": {"id": dependency["spdxLicense"]}}],
                "externalReferences": [{"type": "distribution", "url": dependency["url"]}],
                "hashes": [{"alg": "SHA-256", "content": dependency["sha256"]}],
            }
            for dependency in dependencies
        ],
    }
    path.write_text(json.dumps(sbom, indent=2, sort_keys=True) + "\n")
    return path


def write_provenance(output_dir: Path, commit: str, artifact_entries: list[dict]) -> Path:
    path = output_dir / "docs" / "release" / "provenance" / "local-acr-mvp-provenance.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Local ACR MVP Provenance",
        "",
        f"Source commit: `{commit}`",
        "",
        "No files from examples/ are packaged. The directory is analysis-only.",
        "",
        "## Packaged artifacts",
        "",
    ]
    for artifact in artifact_entries:
        lines.append(f"- `{artifact['path']}` — sha256 `{artifact['sha256']}`")
    path.write_text("\n".join(lines) + "\n")
    return path


def write_release_manifest_template(output_dir: Path, manifest_path: Path) -> Path:
    path = output_dir / "docs" / "release" / "local-acr-mvp-release-manifest.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    relative_manifest = manifest_path.relative_to(output_dir)
    path.write_text(
        "\n".join(
            [
                "# Local ACR MVP Release Manifest",
                "",
                f"Machine-readable manifest: `{relative_manifest}`",
                "",
                "The manifest records artifact SHA-256 digests, source commit, build commands, SBOM path, notice path, provenance path, and examples/ exclusion proof.",
            ]
        )
        + "\n"
    )
    return path


def package_release(repo_root: Path, output_dir: Path, artifacts: Iterable[Path], commit: str) -> PackageResult:
    repo_root = repo_root.resolve()
    output_dir = output_dir.resolve()
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    artifact_entries = copy_artifacts(output_dir, [Path(artifact).resolve() for artifact in artifacts])
    dependencies = load_dependencies(repo_root)
    notice_path = write_notice(repo_root, output_dir, dependencies)
    sbom_path = write_sbom(output_dir, dependencies)
    provenance_path = write_provenance(output_dir, commit, artifact_entries)
    manifest_path = output_dir / "release-manifest.json"
    release_doc_path = write_release_manifest_template(output_dir, manifest_path)

    manifest = {
        "schemaVersion": 1,
        "sourceCommit": commit,
        "artifacts": artifact_entries,
        "buildCommands": BUILD_COMMANDS,
        "noticePath": str(notice_path.relative_to(output_dir)),
        "sbomPath": str(sbom_path.relative_to(output_dir)),
        "provenancePath": str(provenance_path.relative_to(output_dir)),
        "releaseDocumentPath": str(release_doc_path.relative_to(output_dir)),
        "excludedPaths": ["examples/"],
        "examplesExclusionProof": "examples/ is analysis-only and not packaged",
    }
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return PackageResult(output_dir=output_dir, manifest_path=manifest_path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Package Local ACR MVP release artifacts.")
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--artifact", required=True, action="append", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    result = package_release(args.repo_root, args.output_dir, args.artifact, args.commit)
    print(result.manifest_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
