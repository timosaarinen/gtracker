# Release readiness

This document describes the Windows release candidate, not a published release.
The owner changes repository visibility and decides the project license.

## Candidate scope

- Native SID and experimental guitar editors with realtime synthesis.
- SID patterns, arrangement sections, undo/redo, save/load and recovery.
- Genuine SID, WAV, runnable C64 PRG and MP4 visualizer exports.
- Original editable songs, CLI tools, schema and dependency notices.
- 100–400% UI scaling with a 200% default on 4K displays.

## Remaining release gates

1. Choose and add the project license. External GPL/LGPL helpers and libraries
   retain their own license and corresponding-source requirements.
2. On a real Windows device, listen through note entry, full playback, looping,
   pause/resume, stop, output changes and exports. Measure input-to-sound latency
   if making a numerical latency claim. Confirm no clicks or underruns.
3. Test the package on a clean Windows machine with the documented VC++ runtime.
   Exercise save/reopen, recovery, Unicode paths and an unwritable installation.
4. Upload an MP4 manually to the intended service and check its transcode.
5. Only claim Linux/macOS support after running the current candidate there.

## Reproducible candidate build

```sh
python tools/dev.py doctor
python tools/sid_backend.py check
python tools/sid_backend.py prepare-video
python tools/dev.py build --profile desktop
python tools/dev.py test --profile desktop
python tools/sid_stream_verify.py
python tools/dev.py ci
python tools/dev.py package --profile desktop
```

Builds and packages live under `.local/`. Package provenance records exact source
fingerprints, hashes and platform. Include external helper sources, archives,
build recipes and license texts. FFmpeg is currently a separately installed
prerequisite for MP4 export, not a redistributed dependency.

Before publication, review the staged diff, run `python tools/repo_guard.py`,
inspect tracked history for accidental private artifacts, and fetch the remote.
Never rewrite published history to hide a release mistake; fix it in a new commit.

## Release notes draft

This candidate adds low-latency streaming synthesis to both editors, persistent
SID auditions and continuous pattern looping. The MP4 exporter combines title,
three isolated SID scopes and a logarithmic FFT in a shareable 1080p video.
The interface includes sharp scaled fonts and a compact zoom control.

Guitar realism and automatic transcription remain research work. SID presets are
fixed bank 1, PAL/6581 only. Mid-play score edits stop the current snapshot. The
backend build recipe is Windows-specific; this is not a cross-platform release
qualification. There is no account integration or automatic social upload.
