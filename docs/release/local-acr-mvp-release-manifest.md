# Local ACR MVP Release Manifest

Machine-readable manifest: `release-manifest.json`

The generated manifest records:

- source commit;
- staged artifact paths, sizes, and SHA-256 digests;
- build commands used for native, Android, iOS, and packaging verification;
- notice, SBOM, and provenance document paths;
- explicit proof that `examples/` is excluded from release packaging.
