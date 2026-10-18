"""Developer ZIP content/provenance and failed-build publication checks."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import zipfile

from tools import dev, package


ROOT = Path(__file__).resolve().parents[1]


class PackageTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / ".local/tests"
        directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        (self.root / ".gitignore").write_text(".local/\n", encoding="utf-8")
        self.suffix = ".exe" if os.name == "nt" else ""
        names = ["examples/campfire.gtrk.json", "scripts/soften.lua", "schema/pattern.schema.json", "schema/take.schema.json",
                 "schema/capture-result.schema.json", "schema/alignment-result.schema.json", "docs/editor.md", "docs/synthesis.md", "docs/takes.md", "docs/capture.md", "docs/alignment.md", "THIRD_PARTY_NOTICES.md", "packaging/README.txt",
                 ".local/build/headless/bin/gtracker_cli" + self.suffix,
                 ".local/build/headless/_deps/gt_json-src/LICENSE.MIT",
                 ".local/build/headless/_deps/gt_lua-src/src/lua.h", ".local/private.wav"]
        for name in names:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("original generated fixture: " + name, encoding="utf-8")
        self.output = self.root / ".local/packages/test.zip"

    def test_zip_has_matching_hashes_notices_and_no_private_data(self):
        source = dev.source_state(self.root)
        manifest = package.write_package(self.root, "headless", self.output, source)
        self.assertEqual(manifest["source"], source)
        with zipfile.ZipFile(self.output) as archive:
            stored = json.loads(archive.read("gtracker/package.json"))
            self.assertEqual(manifest, stored)
            names = set(archive.namelist())
            self.assertIn("gtracker/THIRD_PARTY_NOTICES.md", names)
            self.assertIn("gtracker/licenses/json.txt", names)
            self.assertIn("gtracker/licenses/lua-header.txt", names)
            self.assertNotIn("gtracker/gtracker_editor" + self.suffix, names)
            self.assertFalse(any("private" in name or ".local/" in name for name in names))
            for entry in manifest["files"]:
                payload = archive.read("gtracker/" + entry["path"])
                self.assertEqual(hashlib.sha256(payload).hexdigest(), entry["sha256"])
                self.assertEqual(len(payload), entry["size_bytes"])
        self.assertEqual(dev.source_state(self.root)["fingerprint_sha256"], source["fingerprint_sha256"])

    def test_rejects_output_outside_local(self):
        with self.assertRaisesRegex(ValueError, "ignored .local"):
            package.write_package(self.root, "headless", self.root / "public.zip", dev.source_state(self.root))
        self.assertFalse((self.root / "public.zip").exists())

    def test_source_change_does_not_replace_previous_package(self):
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b"previous package")
        source = dev.source_state(self.root)
        (self.root / "scripts/soften.lua").write_text("changed during build", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "Source changed"):
            package.write_package(self.root, "headless", self.output, source)
        self.assertEqual(self.output.read_bytes(), b"previous package")
        self.assertEqual(list(self.output.parent.glob("*.tmp")), [])

    def test_failed_build_does_not_replace_previous_package(self):
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b"previous package")
        with mock.patch.object(dev, "run_command", return_value={"returncode": 7}):
            with self.assertRaisesRegex(RuntimeError, "exit 7"):
                package.build_package(self.root, "headless", self.output)
        self.assertEqual(self.output.read_bytes(), b"previous package")


if __name__ == "__main__":
    unittest.main()
