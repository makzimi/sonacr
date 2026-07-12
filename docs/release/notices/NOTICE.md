# Local ACR MVP Notices

Local ACR production code is newly authored for this project.

The legacy `examples/` directory is analysis-only and is not packaged into release artifacts.

Third-party production dependencies are pinned in `third_party/dependencies.lock.json` and their notices are copied into generated release staging by `tools/release/package_local_acr.py`.

## Dependencies

- KISS FFT 131.2.0 — BSD-3-Clause — `third_party/notices/kissfft-COPYING`
- SpeexDSP 1.2.1 — BSD-3-Clause — `third_party/notices/speexdsp-COPYING`
- SQLite 3.53.3 amalgamation — blessing/public-domain style notice — `third_party/notices/sqlite-PUBLIC-DOMAIN.txt`
