"""Owned dummy-editor process interruption; no physical GUI or audio evidence."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

from tools import dev

ROOT=Path(__file__).resolve().parents[1]
EDITOR=Path(os.environ.get("GTRACKER_TEST_EDITOR",dev.executable(ROOT,"desktop","gtracker_editor")))
CLI=Path(os.environ.get("GTRACKER_TEST_CLI",dev.executable(ROOT,"headless","gtracker_cli")))


@unittest.skipUnless(EDITOR.is_file() and CLI.is_file(),"Desktop editor/CLI not built")
class RecoveryProcessTests(unittest.TestCase):
    def setUp(self):
        base=ROOT/".local/tests"; base.mkdir(parents=True,exist_ok=True)
        self.temporary=tempfile.TemporaryDirectory(dir=base); self.addCleanup(self.temporary.cleanup)
        self.directory=Path(self.temporary.name)
        self.env=os.environ.copy(); self.env.update(SDL_VIDEODRIVER="dummy",SDL_RENDER_DRIVER="software",SDL_AUDIODRIVER="dummy")

    def test_forced_owned_process_stop_leaves_a_valid_score_and_original_unchanged(self):
        original=ROOT/"examples/campfire.gtrk.json"; before=original.read_bytes()
        data=self.directory/"user-data"
        command=[str(EDITOR),str(original),"--script",str(ROOT/"scripts/soften.lua"),"--data-dir",str(data),"--resource-dir",str(ROOT)]
        child=subprocess.Popen(command,cwd=ROOT,env=self.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                               creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        try:
            deadline=time.monotonic()+18
            copies=[]
            while time.monotonic()<deadline and child.poll() is None:
                copies=list((data/"recovery").glob("recovery.*.gtrk.json"))
                if copies: break
                time.sleep(.05)
            self.assertEqual(len(copies),1,"No five-second recovery copy appeared in the owned dummy process")
            child.kill()  # Only this Popen-owned process; intentionally skip graceful shutdown.
            stdout,stderr=child.communicate(timeout=5)
            self.assertNotEqual(child.returncode,0,stdout+stderr)
        finally:
            if child.poll() is None: child.kill()
            child.communicate(timeout=5)
        copy=copies[0]; score=json.loads(copy.read_text(encoding="utf-8"))
        self.assertEqual(score["revision"],json.loads(before)["revision"]+1)
        self.assertLess(score["events"][0]["strength"],json.loads(before)["events"][0]["strength"])
        result=subprocess.run([str(CLI),"validate",str(copy),"--json"],cwd=ROOT,capture_output=True,check=True)
        self.assertTrue(json.loads(result.stdout)["ok"])
        preserved=copy.read_bytes()
        screenshot=self.directory/"reopened.bmp"
        result=subprocess.run([str(EDITOR),str(copy),"--data-dir",str(data),"--resource-dir",str(ROOT),"--smoke-test","--frames","8","--screenshot",str(screenshot)],
                              cwd=ROOT,env=self.env,capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertTrue(screenshot.is_file())
        self.assertEqual(copy.read_bytes(),preserved)
        self.assertEqual(original.read_bytes(),before)

    def test_explicit_opt_out_does_not_create_or_scan_recovery_storage(self):
        data=self.directory/"disabled"
        # A non-directory recovery path makes any accidental scan/write fail.
        data.mkdir(); (data/"recovery").write_text("owned opt-out fixture",encoding="utf-8")
        result=subprocess.run([str(EDITOR),str(ROOT/"examples/campfire.gtrk.json"),"--script",str(ROOT/"scripts/soften.lua"),
                               "--data-dir",str(data),"--resource-dir",str(ROOT),"--no-recovery","--smoke-test","--frames","8"],
                              cwd=ROOT,env=self.env,capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual((data/"recovery").read_text(encoding="utf-8"),"owned opt-out fixture")
        self.assertEqual(list(data.iterdir()),[data/"recovery"])


if __name__=="__main__": unittest.main()
