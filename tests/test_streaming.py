"""CLI streaming, original-state previews, progress and cancellation."""
import json
import os
from pathlib import Path
import subprocess
import signal
import sys
import tempfile
import unittest
import wave

from tools import dev

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("GTRACKER_TEST_CLI", dev.executable(ROOT, "headless", "gtracker_cli")))


@unittest.skipUnless(CLI.is_file(), "Native CLI not built")
class StreamingTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / ".local/tests"; directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = ROOT / "examples/slide-study.gtrk.json"
        self.output = self.directory / "output.wav"

    def cli(self, *flags, code=0, source=None):
        result = subprocess.run([str(CLI), "render", str(source or self.source), "--output", str(self.output), *map(str, flags), "--json"],
                                cwd=ROOT, encoding="utf-8", capture_output=True, timeout=20)
        self.assertEqual(result.returncode, code, result.stderr or result.stdout)
        return json.loads(result.stdout), result

    def test_stream_and_warm_preview_match_full_room_pcm(self):
        full, _ = self.cli("--production", ROOT / "examples/room.json")
        original = self.output.read_bytes()
        streamed, progress = self.cli("--production", ROOT / "examples/room.json", "--stream", "--progress-json", "--block-frames", 127)
        self.assertEqual(self.output.read_bytes(), original)
        self.assertEqual(streamed["render_identity_sha256"], full["render_identity_sha256"])
        updates = [json.loads(line) for line in progress.stderr.splitlines()]
        self.assertEqual(updates[0]["processed_frames"], 0)
        self.assertEqual(updates[-1]["written_frames"], full["frames"])
        crop, _ = self.cli("--production", ROOT / "examples/room.json", "--start-frame", 17003, "--end-frame", 91239)
        self.assertEqual(self.output.read_bytes()[44:], original[44 + 17003*4:44 + 91239*4])
        self.assertEqual((crop["frames"], crop["warmup_frames"]), (74236, 17003))
        self.assertFalse(crop["output_reaches_timeline_end"])
        self.assertNotEqual(crop["render_identity_sha256"], full["render_identity_sha256"])

    def test_existing_cancel_and_bad_ranges_preserve_prior_output(self):
        self.output.write_bytes(b"previous completed audio")
        marker = self.directory / "cancel"; marker.touch()
        report, _ = self.cli("--cancel-file", marker, code=130)
        self.assertEqual(report["status"], "cancelled")
        for flags in [("--block-frames", 0), ("--block-frames", 65537), ("--start-frame", -1), ("--start-frame", 1.5), ("--end-frame", 0), ("--start-frame", 100, "--end-frame", 50)]:
            self.cli(*flags, code=2)
        self.assertEqual(self.output.read_bytes(), b"previous completed audio")
        self.assertFalse(list(self.directory.glob("*.tmp-*")))

    def test_long_pattern_streams_beyond_in_memory_bound_and_cancels_in_progress(self):
        pattern = json.loads((ROOT / "examples/campfire.gtrk.json").read_text(encoding="utf-8"))
        pattern.update(length_beats=256, tempo_bpm=20)
        source = self.directory / "long.gtrk.json"; source.write_text(json.dumps(pattern), encoding="utf-8")
        self.output.write_bytes(b"previous completed audio")
        self.cli(source=source, code=2)  # The normal 48k in-memory path is still bounded.
        marker = self.directory / "cancel"
        process = subprocess.Popen([str(CLI), "render", str(source), "--output", str(self.output), "--stream", "--cancel-file", str(marker),
                                    "--progress-json", "--json"], cwd=ROOT, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(lambda: process.kill() if process.poll() is None else None)
        first = json.loads(process.stderr.readline())
        self.assertGreater(first["processing_frames"], 32*1024*1024)
        marker.touch()
        stdout, stderr = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 130, stdout + stderr)
        self.assertEqual(json.loads(stdout)["status"], "cancelled")
        self.assertEqual(self.output.read_bytes(), b"previous completed audio")
        self.assertFalse(list(self.directory.glob("*.tmp-*")))

    def test_wrapper_keeps_progress_on_stderr_and_final_json_on_stdout(self):
        result = subprocess.run([sys.executable, "tools/dev.py", "render", str(self.source), "--output", str(self.output),
                                 "--stream", "--progress-json", "--start-frame", "1234", "--end-frame", "4321", "--json"],
                                cwd=ROOT, encoding="utf-8", capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)
        self.assertEqual(json.loads(result.stdout)["frames"], 3087)
        self.assertEqual(json.loads(result.stderr.splitlines()[-1])["written_frames"], 3087)
        with wave.open(str(self.output), "rb") as audio: self.assertEqual(audio.getnframes(), 3087)

    @unittest.skipIf(os.name == "nt", "POSIX targeted SIGINT; Windows native Ctrl+C needs separate console qualification")
    def test_sigint_cancels_native_and_wrapper_without_replacing_audio(self):
        pattern = json.loads((ROOT / "examples/campfire.gtrk.json").read_text(encoding="utf-8"))
        pattern.update(length_beats=256, tempo_bpm=20)
        source = self.directory / "long.gtrk.json"; source.write_text(json.dumps(pattern), encoding="utf-8")
        existing_markers = set((ROOT / ".local").glob("render-cancel-*"))
        for prefix in ([str(CLI), "render"], [sys.executable, "tools/dev.py", "render"]):
            with self.subTest(prefix=prefix):
                self.output.write_bytes(b"previous completed audio")
                process = subprocess.Popen([*prefix, str(source), "--output", str(self.output), "--stream", "--progress-json", "--json"],
                                           cwd=ROOT, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    self.assertEqual(json.loads(process.stderr.readline())["processed_frames"], 0)
                    process.send_signal(signal.SIGINT)
                    stdout, stderr = process.communicate(timeout=15)
                    self.assertEqual(process.returncode, 130, stdout + stderr)
                    self.assertEqual(json.loads(stdout)["status"], "cancelled")
                    self.assertEqual(self.output.read_bytes(), b"previous completed audio")
                finally:
                    if process.poll() is None: process.kill(); process.communicate()
        self.assertEqual(set((ROOT / ".local").glob("render-cancel-*")), existing_markers)
        self.assertFalse(list(self.directory.glob("*.tmp-*")))


if __name__ == "__main__": unittest.main()
