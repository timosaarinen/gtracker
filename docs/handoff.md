# Current handoff

SID implementation, 2026-10-29, supersedes the earlier document-only checkpoint
`c741361`. The contract remains `docs/sid-tracker-implementation-plan.md`; user
instructions explicitly require the complete native workflow. The new separate
SID mode now includes the document/compiler, original bank, genuine external
GoatTracker packer, exact-SID PCM renderer, prepared auditions, native transport,
keyboard grid/block editing/undo, sections/order/library editing, persistence,
recovery, SID/WAV/SNG/runnable-PRG export, original demo and offline Windows bundle.
See `docs/sid-tracker.md` for controls, bounds and deliberate backend adaptation.
Guitar DSP and existing score semantics are unchanged.

The first inspected console renderer had repeat differences up to four PCM units.
The shipped external GPL adapter instead builds libsidplayfp 3.1.1/reSIDfp 1.2.3
with serial analogue-table construction. It uses no ROMs or user configuration.
`tools/sid_backend.py` pins archives, patches/builds tools, checks hashes and
generates the 579-note bank. Runtime needs no Python/compiler/download. Source
archives, licenses, adapter and recipe accompany the external binaries in ZIPs.

Windows 11 AMD64 / i7-10700K / Clang 18.1.8 Release / Python 3.12.6 evidence:

- Startup doctor passed; CI cadence was not due. The previous full headless CI
  receipt remains `.local/ci/runs/20261029T004736Z-5140b1df/report.json` at 40b9622;
  it does not certify new code.
- `python tools/dev.py build --profile desktop` and CTest passed all 31 native
  desktop cases, including SID document, real synthetic SID widgets, prepared
  PCM/loop/pause/stop and stale-job checks, plus existing guitar UI/capture tests.
  Logs: `.local/sid/desktop-tests.log`, focused follow-ups in `.local/sid/`.
- All 101 Python tests passed (one POSIX-only interrupt skip), recorded in
  `.local/sid/python-tests.log`.
- Final full headless CI passed at the dirty implementation checkpoint in
  `.local/ci/runs/20261029T014542Z-5d06e699/report.json` (22 native / 101 Python,
  one POSIX-only skip). The sequential desktop rebuild and all 31 tests passed
  in `.local/sid/sequential-{build,tests}.log`; a concurrent attempt had hit an
  executable lock while CI capture tests used the desktop binary, so that failed
  build was rerun sequentially. Later dialog/load-error and layout follow-ups use
  focused SID/guitar UI checks. Normal/125% synthetic 980x680 layouts were viewed;
  warm prepared audition queueing measured roughly 8–9 ms with dummy output.
  That timing is software queue evidence, not physical input-to-speaker latency.
- The final desktop guitar render retained campfire SHA-256
  `3313199adea191ad76b5b03c06839dfd473d0677f1e026bc84d77cfa2ce845d9`.
- `python tools/sid_verify.py` checks all eleven presets on all three voices at
  speeds 4/6/12, A4 frequency, post-drum state, boundary HOLD, finite released gates,
  repeated SID bytes and PCM equality. Generated report is
  `.local/sid/verification/report.json`; the measured final-half-second DC-removed
  RMS was about 0.00173 with a declared limit 0.005. This is signal evidence,
  not listening. py65 is an isolated authoring test dependency in `.local/sid/python`.
- VICE 3.10 actually autoloaded/started the BASIC SYS PRG and recorded non-silent
  PCM. A 252-call trace measured mean 19656.004 cycles/play call (instruction
  sampling varies +/-2), one PAL frame. Logs/audio are
  `.local/sid/vice-final{,-trace}.log` and `.local/sid/vice-final.wav`. VICE's
  bounded cycle-limit exit was 1 as documented in its log; it was not a crash.
  Earlier invalid model/monitor-quit attempts failed and are retained separately.
  A subsequent filesystem LOAD/RUN check (autostart PRG mode 0, not RAM injection)
  also passed: 333 player calls averaged 19655.997 cycles, recorded PCM peak 17680
  in signed-16 units. The ROM loader's Searching/Loading/Ready/Starting sequence
  and signal evidence are in `.local/sid/vice-load-run{,-trace}.log`,
  `.local/sid/vice-load-run.wav` and `.local/sid/vice-load-run-report.json`.
- The Windows ZIP is `.local/packages/gtracker-desktop-Windows-AMD64.zip`.
  Relocation to a Unicode/space path with restricted PATH passed all four exports,
  exact SID/PRG music-payload equality, dummy editor launch and unchanged packaged
  hashes (`.local/sid/relocation-report.json`). An ACL-enforced read-only install
  also rendered a fresh uniquely named score to exclude a preview-cache hit;
  temporary ACLs were restored (`.local/sid/readonly-fresh-report.json`).
  Implementation commit: `8e97f01`. Its clean-source package (653 payload files)
  was re-extracted and verified again with restricted PATH, a full SID render,
  dummy editor and unchanged packaged hashes in
  `.local/sid/published-package-report.json`. Final dialogs/load-error guards
  passed six focused checks in `.local/sid/release-focused.log`; the CLI audition
  and validation follow-up passed after its final help/argument-bound correction.

The package's own source fingerprint records its exact build inputs; final focused
checks/refresh accompany publication. These Windows results do not qualify Linux
or Intel Mac. Physical Windows interaction was attempted through the computer-use
skill, but its native pipe was unavailable even after retry/reset. No human
listening or physical SID hardware assessment is claimed. Those environmental
checks remain to qualify musical balance, audition feel and real display/input;
do not relabel synthetic tests or VICE file audio as physical listening.

The independent original example is about 123 seconds, with seven reusable phrases
and sixteen placements through intro/heavy/mellow/return/outro. Generated audio,
SID/PRG exports, all tools and private evidence stay ignored. Useful next step:
perform the guide's real-window/listening journey on the delivered package,
especially the heavy/mellow transitions and isolated-loop smoothing.

Night shift, 2026-10-18, requested eight-hour window starting about 01:51 UTC.
This handoff records the implementation and checks through the final recovery
follow-up around 09:41 UTC; the delivery index records the last completed runs.
Read `README.md`, `AGENTS.md` and the relevant issue before editing. Local checks
are authoritative; generated/private files stay in `.local/`.

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

The main feature checkpoint is **239e599**. It passed Windows full CI and both
platforms' 101-test Python suites, plus the seven-case generated evaluation.
The later delivery checkpoint **67d7d08** passed both full local CI runs. A final
Windows follow-up fixes recovery when an existing folder is spelled `Recovery`:
canonical spelling is retained only for the expected child of user data. Original
scores stay protected. Native recovery/UI/fixtures4 and owned-process tests2
passed after the fix; current clean follow-up receipts and artifact hashes are
recorded in `.local/nightshift/final-evidence.json`. Core/production/renderer DSP
has not changed during these final editor/authoring follow-ups.

| Platform/toolchain | Actual evidence |
| --- | --- |
| Windows 11 AMD64, i7-10700K, Clang 18.1.8 Release, Python 3.12.6 | 21 headless / 101 Python tests at `239e599` (one POSIX-only interrupt skip); 29 desktop tests at `44ed22e`; 7 dependency-free core tests at `239e599` |
| Ubuntu20.04.6 WSL x86_64, GCC 9.4 Release, Python 3.11.9 | 21 headless / 29 desktop tests at `44ed22e`; 101 Python tests and 7 generated evaluation cases at `239e599`, without skips |
| Same Ubuntu, Clang 10 Debug ASan/UBSan | 21 headless tests; separately compiled actual editor UI + 2 fixtures passed CTest with the 180-second limit (UI: 50.18s) |

Windows CI receipt: `.local/ci/runs/20261018T090834Z-30daa0a7/report.json`.
Ubuntu CI receipt: `.local/qualification/ubuntu/.local/ci/runs/20261018T085759Z-ce545e54/report.json`.
Other logs: `.local/nightshift/windows-evaluation-ci.log`, `.local/nightshift/windows-layout-desktop.log`,
`.local/qualification/ubuntu/.local/layout-{desktop,clang}.log`,
`.local/qualification/ubuntu/.local/layout-editor-asan-ui.log`.
The current core-only log is `.local/nightshift/delivery-core.log`; Ubuntu
Python/evaluation logs use `.local/qualification/ubuntu/.local/evaluation-`.

At `239e599`, both Windows/Linux desktop ZIPs (53 payload files plus manifest)
passed manifest/hash checks after moving
into Unicode/space paths and launching from an unrelated directory. Windows ACL
write/create denial was enforced on private extracted copies; the Linux audit
also proved that denial through WSL. CLI transform/save/validate, exact song
transition preview, cache miss/hit and dummy editor smoke preserved bundled
bytes. Windows additionally exercised native proposal inspection and owned
interruption recovery. Temporary ACLs were restored. Reports:
`.local/nightshift/relocation-52c94f0e/report.json` and
`.local/nightshift/linux-relocation-1b97bb54/report.json`.
Both headless ZIPs also built (43 payload files plus manifest). Final refreshed
archive hashes/provenance belong in `.local/nightshift/final-evidence.json`.
An obsolete early preview ZIP is preserved under `.local/nightshift/archive/`.
Packages require host runtimes and are developer artifacts, not a public release.

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
claimed. `python -m analysis.evaluate --output .local/evaluations/NEW --json` now
repeats the seven conditions, source/engine provenance and explicit pitch/onset
metrics. `.local/evaluations/proposals-nightshift-venv-0907/report.json` completed
all seven cases; its separate oracle reconstruction uses authored labels to
reject the extra candidate and restore pitch/onsets/strength/damping. The result
is byte-identical only after those known-label corrections, which are neither
inference nor human review and are excluded from accuracy metrics. Known-score model probes are conditioned on expected pitch and their
20dB RMS-drop measurement is not T60. Performance measurements name the Windows
machine/workload in `docs/retained.md` and local receipts.

The old GCC9 ASan runtime in this WSL install repeatedly failed inside its runtime
with recursive diagnostics; it is not a passing code check. Use the verified
Clang 10 build at `.local/build/sanitize-clang` in the isolated Ubuntu clone.
An extra instrumented desktop test initially reached CTest's 40-second limit;
the same workflow passed directly with 180 seconds. CMake now sets 180 seconds
for sanitizer `editor_ui` only; normal desktop limits stay 40. Early new export
test failures (a popup query outside an ImGui window, stale fixed test storage,
and invalid schema/length fixtures) were corrected before the passing checks.
The separate native input audit rejected 39 generated malformed/out-of-range
patterns with structured errors on Windows Release and Clang 10 ASan/UBSan; a
privately copied source also survived an attempted output alias. Reports are
`.local/nightshift/native-input-audit/{report,clang-asan-report}.json`. This
bounded audit supplements the suites; it is not exhaustive fuzzing.

Next, complete physical Windows/Linux playback/capture and Intel Mac checks;
then collect consented real guitar takes with timing/source uncertainty intact.
Use the shared renderer and selectable models to compare dry sound before
expanding contact physics or transcription. Arrangement UI and editor capture
can build on the existing native/CLI contracts. Keep generated evidence local,
review staged diffs, run `python tools/repo_guard.py`, and fetch/inspect remote
before publishing further changes.
