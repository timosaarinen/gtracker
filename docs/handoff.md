# Current handoff

Night shift, 2026-10-18, 01:51–09:51 UTC. Work is still in progress until the
requested end time. Read `README.md`, `AGENTS.md` and the relevant issue before
editing. Local checks are authoritative; generated/private files stay in `.local/`.

## Shipped foundations

Small verified commits are on `main`; no published history was rewritten.
The independent C/C++ instrument and original C ABI remain separate from SDL,
ImGui, Lua, Python and JSON. Existing baseline/contact paths stay selectable;
the original campfire PCM16 hash is unchanged.

| Issue | Implemented work | Remaining scope |
| --- | --- | --- |
| #1 | Guarded load/close, grouped undo, content savepoints, UTF-8 paths/errors, relocatable developer ZIPs, cancellable previews, keyboard fret entry, recovery score copies, session playback-output selection, background WAV export | Physical window/input/audio, actual OS display scaling, native Linux desktop and Intel Mac qualification |
| #2 | Preserved take import, native PCM/hash verification, explicit source-sample timing, aligned source/synth overlays and A/B, native capture CLI/controller with retained partials, generated loopback reference and bounded file-delay estimation | Real microphone/loopback measurements, device clock drift and editor recording controls |
| #3 | Explicit pattern v2 slides, copied native curves, linked initial frets, fractional timing, Lua/CLI/editor save/reopen and undo | Additional articulations, continuous controls, course-aware instruments |
| #4 | Separately selectable contact/retained-excitation experiments, original dry repluck study, known-score pitch/decay probes | Physical pick/fret/contact geometry, real-guitar comparisons and listening assessment |
| #5 | Unchanged metadata-only worker plus a separate optional monophonic proposal job, source/snapshot freshness, explicit fingering review and one-batch acceptance/undo | General transcription, chords/articulations, actual played-string inference and real-guitar accuracy |
| #6 | Shared native continuous renderer, bounded streamed export/crops/cancellation, reusable song arrangements/portable snapshots, checked persistent PCM cache, dependency-free runtime example | Arrangement UI, seamless loop preparation and acoustic checkpoints |
| #7 | Native stereo algorithmic room, saved production settings, gain/tails/metrics and exact dry bypass through CLI/editor/runtime | Listening qualification and further sound development |

Renderer version is **0.5.0**. Pattern v1 keeps the implicit original baseline;
v2 explicitly chooses a model. Take/metadata, song, production, proposal and cache
formats have separate versions. No recordings, weights, third-party binaries or
WAVs were committed. All issues remain open where their acceptance needs more
work; no hosted workflow, public release or issue communication was triggered.

## Latest editor polish

- Recovery writes only each session's unique normal score copy every five seconds
  while unsaved work changes. Originals stay unchanged. Interrupted copies can
  be reviewed and saved to a new name; undo/transport/take association are not
  recovered. `--no-recovery` disables that session. Write failures stay visible.
- Audio output selects system default or a named device for this session. Changes
  stop queued sound/autoplay without changing the score or render. Missing named
  outputs require a new choice. Source/synth keep their native input formats.
- Export WAV streams an immutable score snapshot on an owned worker. Progress,
  cancel, replacement confirmation, source/alias guards, failures and shutdown
  are covered. It writes PCM16/48 kHz with the score's room/tails. Edits belong
  to the next export; cancellation cannot retract an already completed file.
- Toolbar/title/source details now wrap. Twelve independent lanes keep readable
  spacing in a vertically scrolling tab, with keyboard cursor reveal and the
  waveform below. Wheel input retains time zoom/pan. Minimum-window 980×680
  captures at normal/125% programmatic text/style scale were inspected. These
  are synthetic layout checks, not physical display/input qualification.

## Verified source and platforms

The last clean fully qualified checkpoint is **9b01b08**. Subsequent layout
polish passed Windows desktop29, then focused editor UI/fixtures3 after the
sanitizer-only timeout adjustment. Its source fingerprint and framebuffer names
are in `.local/nightshift/layout-checks.json`; clean platform runs follow its
committed checkpoint.

| Platform/toolchain | Actual evidence at 9b01b08 |
| --- | --- |
| Windows 11 AMD64, i7-10700K, Clang18.1.8 Release, Python3.12.6 | Headless21; Python96 with one POSIX-only interrupt skip; desktop29; dependency-free core-only7 |
| Ubuntu20.04.6 WSL x86_64, GCC9.4 Release, Python3.11.9 | Headless21; Python96 without skips; desktop29 |
| Same Ubuntu, Clang10 Debug ASan/UBSan | Headless21; separately compiled actual editor UI passed under an explicit180-second bound |

Windows CI receipt: `.local/ci/runs/20261018T083236Z-0244117f/report.json`.
Ubuntu CI receipt: `.local/qualification/ubuntu/.local/ci/runs/20261018T083259Z-7be952f2/report.json`.
Other logs: `.local/nightshift/windows-final-{ci,core}.log`,
`.local/qualification/ubuntu/.local/final-{desktop,clang}.log`,
`.local/qualification/ubuntu/.local/desktop-asan-ui-direct.log`.

Both 53-file Windows/Linux desktop ZIPs passed manifest/hash checks after moving
into Unicode/space paths and launching from an unrelated directory. Windows ACL
write/create denial was enforced on private extracted copies; the Linux audit
also proved that denial through WSL. CLI transform/save/validate, exact song
transition preview, cache miss/hit and dummy editor smoke preserved bundled
bytes. Windows additionally exercised native proposal inspection and owned
interruption recovery. Temporary ACLs were restored. Reports:
`.local/nightshift/relocation-3a86c721/report.json` and
`.local/nightshift/linux-relocation-d680716c/report.json`.
The 43-file Windows headless ZIP also built. Packages require host runtimes and
are developer artifacts, not a public release.

Native compilation, synthetic UI/dummy playback, real window interaction,
listening and microphone capture remain separate evidence. **No physical
window, listening, microphone, latency/drift or Intel Mac pass is claimed.**
The token cannot list Actions (HTTP403); hosted qualification was not run.

## Playable original audio

All WAVs are generated under ignored `.local/`:

| File | Parameters/result |
| --- | --- |
| `night-watch.wav` | Original 192-beat/12-placement fingerstyle study, seed4242, 96BPM, retained_0_5, room_v1, 48kHz stereo PCM16, 124.966s, peak0.21115, no clipping, quiet final tail |
| `night-watch-dry.wav` | Same score, exact production bypass, 48kHz mono PCM16, 123s |
| `demo.wav` | Original campfire, seed42, 90BPM, baseline_0_1, 48kHz mono PCM16, 7.333s |
| `night-shift-retained.wav` | Earlier original 46.468s retained-model stereo arrangement |
| `repluck-contact.wav` / `repluck-retained.wav` | Original dry A/B using explicit models |

Night Watch's five editable source files ship in the ZIPs. Native validation,
portable-snapshot/997-frame block equality, source preservation and an exact
transition preview passed. Windows Clang and Linux GCC produced byte-identical
PCM16 for campfire and both Night Watch renders; this establishes those tested
inputs, not every compiler/parameter combination. Reports:
`.local/nightshift/night-watch-checks/report.json` and
`.local/nightshift/cross-platform-pcm.json`.

Campfire SHA256: `3313199adea191ad76b5b03c06839dfd473d0677f1e026bc84d77cfa2ce845d9`.
Night Watch stereo: `c9a9b3d35a1af8ebec3074a5375e04b6cbdb8d89d8fe9070de8bca95ab8ef5c3`.
No listening judgement accompanies the signal checks.

## Limits and useful next work

Optional monophonic proposals are a research baseline. Generated clean16/44.1kHz
fixtures yielded four near-pitch notes; clean48kHz and low-noise/stereo variants
exposed an extra octave-low attack candidate, and a chord yielded misleading
notes. These failures remain in
`.local/nightshift/proposal-evaluation-eb243d7b/report.json` and `docs/proposals.md`.
Nothing is preaccepted. No ML weights were downloaded or real-guitar accuracy
claimed. Known-score model probes are conditioned on expected pitch and their
20dB RMS-drop measurement is not T60. Performance measurements name the Windows
machine/workload in `docs/retained.md` and local receipts.

The old GCC9 ASan runtime in this WSL install repeatedly failed inside its runtime
with recursive diagnostics; it is not a passing code check. Use the verified
Clang10 build at `.local/build/sanitize-clang` in the isolated Ubuntu clone.
An extra instrumented desktop test initially reached CTest's40-second limit;
the same workflow passed directly with180 seconds. CMake now sets180 seconds
for sanitizer `editor_ui` only; normal desktop limits stay40. Early new export
test failures (a popup query outside an ImGui window, stale fixed test storage,
and invalid schema/length fixtures) were corrected before the passing checks.
A deeply nested invalid pattern also failed cleanly in a separate parser audit.

Next, complete physical Windows/Linux playback/capture and Intel Mac checks;
then collect consented real guitar takes with timing/source uncertainty intact.
Use the shared renderer and selectable models to compare dry sound before
expanding contact physics or transcription. Arrangement UI and editor capture
can build on the existing native/CLI contracts. Keep generated evidence local,
review staged diffs, run `python tools/repo_guard.py`, and fetch/inspect remote
before publishing further changes.
