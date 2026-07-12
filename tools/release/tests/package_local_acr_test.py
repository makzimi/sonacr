import hashlib
import importlib.util
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
PACKAGER_PATH = REPO_ROOT / "tools" / "release" / "package_local_acr.py"


def load_packager():
    spec = importlib.util.spec_from_file_location("package_local_acr", PACKAGER_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class PackageLocalAcrTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = Path(tempfile.mkdtemp(prefix="local-acr-release-test-"))
        self.addCleanup(lambda: shutil.rmtree(self.temp_dir, ignore_errors=True))
        self.packager = load_packager()
        self.inputs = self.temp_dir / "inputs"
        self.inputs.mkdir()
        (self.inputs / "android-debug.apk").write_bytes(b"android-apk")
        (self.inputs / "shared-debug.aar").write_bytes(b"shared-aar")
        (self.inputs / "LocalAcrShared.framework.zip").write_bytes(b"ios-framework")
        (self.inputs / "local_acr_db").write_bytes(b"native-cli")
        (self.inputs / "venue-demo.lacrdb").write_bytes(b"database")
        self.output = self.temp_dir / "release"

    def test_manifest_lists_artifacts_with_sha256_and_build_commands(self):
        result = self.packager.package_release(
            repo_root=REPO_ROOT,
            output_dir=self.output,
            artifacts=[
                self.inputs / "android-debug.apk",
                self.inputs / "shared-debug.aar",
                self.inputs / "LocalAcrShared.framework.zip",
                self.inputs / "local_acr_db",
                self.inputs / "venue-demo.lacrdb",
            ],
            commit="abc123",
        )

        manifest = json.loads(result.manifest_path.read_text())
        self.assertEqual(manifest["schemaVersion"], 1)
        self.assertEqual(manifest["sourceCommit"], "abc123")
        self.assertEqual(len(manifest["artifacts"]), 5)
        self.assertIn("examples/", manifest["excludedPaths"])
        self.assertEqual(manifest["examplesExclusionProof"], "examples/ is analysis-only and not packaged")
        self.assertGreaterEqual(len(manifest["buildCommands"]), 5)

        staged_names = {entry["path"] for entry in manifest["artifacts"]}
        self.assertEqual(
            staged_names,
            {
                "artifacts/android-debug.apk",
                "artifacts/shared-debug.aar",
                "artifacts/LocalAcrShared.framework.zip",
                "artifacts/local_acr_db",
                "artifacts/venue-demo.lacrdb",
            },
        )
        for entry in manifest["artifacts"]:
            staged_path = self.output / entry["path"]
            self.assertTrue(staged_path.exists(), entry["path"])
            self.assertEqual(hashlib.sha256(staged_path.read_bytes()).hexdigest(), entry["sha256"])

    def test_release_docs_include_dependency_notices_sbom_and_provenance(self):
        result = self.packager.package_release(
            repo_root=REPO_ROOT,
            output_dir=self.output,
            artifacts=[self.inputs / "local_acr_db"],
            commit="abc123",
        )

        notice = (self.output / "docs/release/notices/NOTICE.md").read_text()
        sbom = json.loads((self.output / "docs/release/sbom/local-acr-mvp-sbom.json").read_text())
        provenance = (self.output / "docs/release/provenance/local-acr-mvp-provenance.md").read_text()
        manifest_template = (self.output / "docs/release/local-acr-mvp-release-manifest.md").read_text()

        for dependency in ("kissfft", "speexdsp", "sqlite"):
            self.assertIn(dependency, notice)
            self.assertTrue(any(component["name"] == dependency for component in sbom["components"]))
        self.assertIn("abc123", provenance)
        self.assertIn("No files from examples/ are packaged", provenance)
        self.assertIn(str(result.manifest_path.relative_to(result.output_dir)), manifest_template)

    def test_cli_packages_with_explicit_arguments(self):
        completed = subprocess.run(
            [
                "python3",
                str(PACKAGER_PATH),
                "--repo-root",
                str(REPO_ROOT),
                "--output-dir",
                str(self.output),
                "--commit",
                "abc123",
                "--artifact",
                str(self.inputs / "local_acr_db"),
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("release-manifest.json", completed.stdout)
        self.assertTrue((self.output / "release-manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
