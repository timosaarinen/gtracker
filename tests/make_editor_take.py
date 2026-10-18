"""Generate the native renderer's original phrase as a source-take UI fixture."""

from array import array
import argparse
from pathlib import Path
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import dev, takes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", choices=dev.PROFILES, default="desktop")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / ".local").resolve()):
        raise ValueError("Generated UI take fixtures must stay under ignored .local/.")
    marker = ".generated-editor-take"
    known = {"source.wav", "pattern.gtrk.json", "take.json", "analysis.json", marker}
    if output.exists():
        if not (output / marker).is_file() or {path.name for path in output.iterdir()} != known:
            raise ValueError("Refusing to replace a directory that is not this generated fixture")
        for name in known:
            (output / name).unlink()
        output.rmdir()
    mono_path = output.with_name("editor-source-render.wav")
    source_path = output.with_name("editor-take-source.wav")
    subprocess.run([str(dev.executable(ROOT, args.profile, "gtracker_cli")), "render",
                    str(ROOT / "examples/campfire.gtrk.json"), "--sample-rate", "44100",
                    "--output", str(mono_path), "--json"], check=True, capture_output=True)
    with wave.open(str(mono_path), "rb") as source:
        mono = array("h", source.readframes(source.getnframes()))
        if sys.byteorder != "little": mono.byteswap()
    stereo = array("h", [0]) * (44100 * 2)
    for index, sample in enumerate(mono):
        stereo.append(sample)
        stereo.append(round(mono[index - 3] * 0.8) if index >= 3 else 0)
    if sys.byteorder != "little": stereo.byteswap()
    with wave.open(str(source_path), "wb") as output_audio:
        output_audio.setframerate(44100)
        output_audio.setnchannels(2)
        output_audio.setsampwidth(2)
        output_audio.writeframes(stereo.tobytes())
    takes.import_take(source_path, ROOT / "examples/campfire.gtrk.json", output,
                      zero_frame=44100, job_id="generated-editor-source", profile=args.profile)
    (output / marker).write_text("Original generated test material; safe fixture ownership marker.\n", encoding="utf-8")
    print("Generated synthetic stereo source at 44.1 kHz with one-second pre-roll:", output)


if __name__ == "__main__":
    main()
