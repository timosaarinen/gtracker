"""Generate a native single-note take and actual optional research proposal job."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from analysis import propose
from tools import dev, takes


def generate(output: Path, profile: str):
    output = output.resolve()
    if not output.is_relative_to((ROOT/".local").resolve()): raise ValueError("Generated fixture must stay under .local/")
    marker = ".generated-editor-proposal"
    known = {marker,"take.json","source.wav","pattern.gtrk.json","analysis.json","proposals.json"}
    if output.exists():
        if not (output/marker).is_file() or {file.name for file in output.iterdir()} != known:
            raise ValueError("Refusing to replace a directory that is not this generated proposal fixture")
        for name in known: (output/name).unlink()
        output.rmdir()
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=output.name+"-inputs-",dir=output.parent) as temporary:
        source, pattern_path = Path(temporary)/"source.wav", Path(temporary)/"empty-pattern.gtrk.json"
        score = ROOT/"examples/monophonic-study.gtrk.json"
        native = json.loads(subprocess.run([str(dev.executable(ROOT,profile,"gtracker_cli")),"render",str(score),"--sample-rate","16000",
                                           "--tail","0.5","--output",str(source),"--json"],capture_output=True,encoding="utf-8",check=True).stdout)
        pattern = json.loads(score.read_text(encoding="utf-8")); pattern["events"] = []
        dev.atomic_json(pattern_path,pattern)
        takes.import_take(source,pattern_path,output,selection_end=64000,job_id="generated-monophonic-review",profile=profile)
    proposals = propose.propose_take(output/"take.json",channel=1,job_id="generated-monophonic-proposals",profile=profile)
    dev.atomic_json(output/"proposals.json",proposals)
    (output/marker).write_text("Original native synthetic phrase; no real guitar or listening qualification.\n",encoding="utf-8")
    notes = proposals["notes"]
    if len(notes) != 4 or any(abs(note["pitch_midi"]-midi) > .25 for note,midi in zip(notes,(40,47,52,55))):
        raise ValueError("Known original monophonic fixture did not produce four near-pitch candidates")
    print(json.dumps({"ok":True,"fixture":str(output),"native":native,"notes":[{"onset_frame":n["onset_frame"],"pitch_midi":n["pitch_midi"],"alternatives":len(n["alternatives"])} for n in notes]}))


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True); parser.add_argument("--profile",choices=dev.PROFILES,default="desktop")
    args=parser.parse_args(); generate(args.output,args.profile)
