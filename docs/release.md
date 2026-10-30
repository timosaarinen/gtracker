# Release readiness

This document describes Windows/Linux release preparation, not a published release.
The source repository can be public while binary builds remain experimental
prereleases. The owner changes repository visibility. Project code is MIT, with explicit
GPL helper exceptions and preserved dependency licenses.

## Candidate scope

- Native SID and experimental guitar editors with realtime synthesis.
- SID patterns, arrangement sections, undo/redo, save/load and recovery.
- Genuine SID, WAV, runnable C64 PRG and MP4 visualizer exports.
- Original editable songs, CLI tools, schema and dependency notices.
- 100–400% UI scaling with a 200% default on 4K displays.

## Pre-publication audit, 2026-10-30

Audited the implementation based on `922371b`, including issue #1, save/export
paths, realtime transport, package contents, dependency notices and public docs.
Fixed SID Save As colliding with an active export destination, and reset overwrite
confirmation when the filename changes. Package verification now rejects unlisted
payloads, case-colliding names, noncanonical paths, links and checksum mismatches
before extraction or execution. Corrected outdated Windows-only setup claims.

Windows 11 passed 31 native desktop tests and 110 Python tests (one POSIX-only
skip). Realtime SID comparison again observed at most 3 PCM16 units difference
over 520,343 frames, twice. Ubuntu 20.04.6 WSL2 rebuilt the desktop and passed both
SID native tests and 10 package/verifier tests. These checks cover the working
fixes; package manifests identify the exact packaged source revision separately.

A scan of all 548 reachable historical Git blobs at the baseline found no matches
for the audited credential patterns, binary blobs or forbidden artifact paths.
All tracked Markdown local-file links resolved. This is a bounded audit, not a
guarantee that every possible secret or defect has been detected. Local evidence:
`.local/prerelease-history.json`, `.local/prerelease-{build,tests,python,stream}.log`;
Linux evidence: `audit-{build,tests}.log` beside the isolated WSL checkout.

The source is ready for public review as an experimental project. Binary builds
remain prerelease candidates with the device and platform limits below.

## Remaining release gates

Local Linux evidence (2026-10-30): Ubuntu 20.04.6 x86_64 under WSL2, GCC 9.4,
CMake 4.3.3 and Python 3.10.14. The desktop suite passed 31 tests; local CI passed
22 native and 105 Python tests. Persistent SID output matched offline output
within the documented 4-unit PCM16 tolerance (observed maximum 3 over 520,343
frames, twice). The relocated package exported SID/PRG/WAV/MP4, launched both
editors with dummy devices, and retained unchanged installation hashes.
This is automated candidate evidence, not a physical listening qualification.

1. On a real Windows device, listen through note entry, full playback, looping,
   pause/resume, stop, output changes and exports. Measure input-to-sound latency
   if making a numerical latency claim. Confirm no clicks or underruns.
2. Test the package on a clean Windows machine with the documented VC++ runtime.
   Exercise save/reopen, recovery, Unicode paths and an unwritable installation.
3. Upload an MP4 manually to the intended service and check its transcode.
4. Linux qualification uses a separate Ubuntu WSL2 checkout and the
   [Linux package checks](linux.md). WSL2 dummy tests cannot establish native
   desktop device quality or compatibility with every distribution.
5. macOS release is postponed. Verify on the owner's Intel MBP16 before preparing
   an Intel candidate; Apple Silicon requires its own native qualification.

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
build recipes and license texts. FFmpeg is an optional external download for MP4
export, not a redistributed dependency. Windows includes an opt-in, checksum-pinned
download helper; other platforms use manual installation.

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
backend build recipes support Windows and Linux; macOS is deferred. Automated
checks do not establish physical-device qualification. There is no account
integration or automatic social upload.
