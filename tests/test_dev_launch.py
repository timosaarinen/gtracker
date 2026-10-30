"""Development launch must build successfully before running an existing binary."""
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import dev


class DevelopmentLaunchTests(unittest.TestCase):
    def test_ui_scale_is_forwarded_to_native_editor(self):
        with mock.patch.object(dev,"run_command",return_value={"returncode":0}) as run:
            self.assertEqual(dev.main(["dev","--guitar","--ui-scale","4"]),0)
            command=run.call_args.args[1]
            self.assertEqual(command[command.index("--ui-scale")+1],"4.0")

    def test_failed_build_does_not_launch_stale_executable(self):
        with mock.patch.object(dev, "run_command", side_effect=[{"returncode": 0}, {"returncode": 23}]) as run:
            self.assertEqual(dev.main(["dev", "--guitar"]), 23)
        self.assertEqual(run.call_count, 2)
        self.assertEqual(run.call_args_list[-1].args[1][:2], ["cmake", "--build"])

    def test_default_sid_uses_repo_resources_and_checks_prepared_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / ".local/sid/tools/backend.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text("{}")
            (manifest.parent / "video.json").write_text("{}")
            editor = root / ".local/build/desktop/bin" / ("gtracker_editor.exe" if dev.os.name == "nt" else "gtracker_editor")
            editor.parent.mkdir(parents=True)
            editor.write_bytes(b"mock built executable")
            with mock.patch.object(dev, "ROOT", root), mock.patch.object(dev, "run_command", return_value={"returncode": 0}) as run:
                self.assertEqual(dev.main(["dev", "--frames", "3"]), 0)
            commands = [call.args[1] for call in run.call_args_list]
            self.assertEqual(commands[2][-1], "check")
            self.assertIn("--sid", commands[3])
            self.assertEqual(commands[3][-4:], ["--data-dir", ".local/dev", "--resource-dir", str(root)])

    def test_guitar_file_skips_sid_setup_and_backend_failure_stops_launch(self):
        with mock.patch.object(dev, "run_command", return_value={"returncode": 0}) as run:
            self.assertEqual(dev.main(["dev", "examples/campfire.gtrk.json"]), 0)
            self.assertEqual(run.call_count, 3)
            self.assertNotIn("--sid", run.call_args.args[1])
        with mock.patch.object(dev, "run_command", side_effect=[{"returncode": 0}, {"returncode": 0}, {"returncode": 7}]) as run:
            self.assertEqual(dev.main(["dev", "--sid"]), 7)
            self.assertEqual(run.call_count, 3)


if __name__ == "__main__":
    unittest.main()
