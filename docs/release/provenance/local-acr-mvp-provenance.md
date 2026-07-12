# Local ACR MVP Provenance

Source commit: populated by `tools/release/package_local_acr.py` during release staging.

Production code is newly authored for Local ACR. Legacy reference projects under `examples/` are analysis-only.

No files from examples/ are packaged.

The release packager records artifact SHA-256 digests, build commands, dependency notices, SBOM path, and provenance path in `release-manifest.json`.
