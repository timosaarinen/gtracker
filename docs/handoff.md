# Current handoff

Initial release preparation, 2026-10-30: owner requested the first GitHub release.
Version `v0.5.0-rc.1` follows CMake project version 0.5.0 and is explicitly a
prerelease. Windows x64 and Linux x86_64 portable packages include original scores,
SID helpers and corresponding source/license material. macOS remains deferred.
README and Pages now link to versioned downloads. Package provenance and separate
verification reports identify the exact clean source revision; release assets
include SHA-256 checksums. Physical listening and clean-machine limits remain.

GitHub Pages, 2026-10-30: added a static SID-first site in `docs/index.html` and
`docs/site.css`, reusing the banner, with no emojis, scripts, analytics or external
fonts. `docs/.nojekyll` serves these files directly. Publication uses GitHub's
main-branch `/docs` Pages source; once enabled, future pushes update it independently
of the manual native qualification workflow. Activation was blocked by GitHub
HTTP 403 (token lacks Pages setting permission); GET still returns 404. Owner
must select Settings > Pages > Deploy from a branch > main > /docs > Save.
Expected URL: https://timosaarinen.github.io/gtracker/ . README links there. Local HTML
structure, fragment/resource/manual links, image metadata, emoji scan and HTTP
asset checks passed. Browser preview was unavailable (no connected browsers),
so no visual desktop/mobile browser qualification is claimed. Mobile layout and
reduced-motion styles are included. No native application changes.

Public README, 2026-10-30: owner made the repository public and requested a
musician-facing C64 SID introduction, the opening nostalgia questions, the
unqualified "The Ultimate SID Tracker" headline, and an illustrated banner.
Guitar authoring is now a secondary note for the owner's MMO, Lust of War.
README keeps actual setup requirements in a disclosure and accurately notes
that GitHub binary releases have not been published. Generated banner lives in
`docs/assets/gtracker-banner.png`, with tool/prompt provenance beside it. The
owner's explicit artwork request is a narrow exception to text-only assets:
repo_guard permits only this exact reviewed SHA-256, capped at 4 MiB; regression
coverage rejects altered or unrelated binaries. No application/audio changes.

Pre-publication audit, 2026-10-30, based on `922371b`: read issue #1 and reviewed
save/export, realtime, packaging and public documentation. Fixed SID Save As
colliding with an active export destination (native regression), and reset
overwrite confirmation on filename edits. Package verifier now validates complete
manifest coverage and canonical unique regular-file paths before extraction;
five new Python tests cover corruption, extra files, links and path ambiguity.
Windows desktop build + all 31 native tests passed; all 110 Python tests passed
with one platform skip. SID stream comparison: 520,343 frames twice, max PCM16
difference 3; warm first block 13.1/13.2 ms, excluding compilation/device latency.
WSL2 Ubuntu rebuilt desktop and passed two SID native + 10 package/tool tests.
History scan: 548 reachable blobs, no audited credential-pattern hits, binary
blobs or forbidden paths; no broken tracked Markdown local links. Evidence under
`.local/prerelease-*`; WSL logs beside the isolated checkout. Public documentation
now separates source publication from binary/device qualification. Physical
listening, clean-machine qualification and Intel macOS remain outstanding;
no repository visibility change or public binary release was performed.

Linux preparation, 2026-10-30, based on `406c755`: added native pinned SID
backend builds, Linux desktop bundle with executable launchers, cross-platform
stream verifier, and reusable package verification. `bun run dev`/Python launcher
can prepare Linux SID tools. Windows helper compilation was checked separately
under `.local/sid/windows-recipe-check`, leaving installed helpers intact.
Windows build, SID dummy test and nine package/launcher tests passed.
Ubuntu 20.04.6 WSL2 x86_64 (GCC 9.4, CMake 4.3.3, Python 3.10.14) in the isolated
`/home/tims/.local/share/gtracker-release/repo` checkout passed all 31 desktop
tests, then local CI (22 native, 105 Python; report
`.local/ci/runs/20261030T092327Z-902917b4/report.json`). Initial headless CI exposed
tests assuming an already-built editor; fixed executable fixtures and proper
skip detection. Real stream verification compared 520,343 frames twice with
max difference 3 PCM16 units. Warm first block ~11.7 ms, helper startup ~299 ms,
excluding compilation/device latency. Relocated Linux package exported all four
formats, launched SID/guitar with dummy devices and left installation unchanged.
Logs live in `/home/tims/.local/share/gtracker-release/`; package evidence under
the checkout's `.local/package-checks/`. Manual hosted workflow now prepares and
checks Linux packages; Windows packaging stays local with MSYS2. No hosted run
or physical-device qualification claimed. macOS release is explicitly postponed
for the owner's Intel MBP16; Apple Silicon remains unqualified.

FFmpeg setup, 2026-10-30, based on `65b7101`: Windows More > Set up MP4 export
offers a cancellable, SHA-256-pinned Gyan 9.0.2 download into the data folder.
No admin/PATH changes; fixed-path extraction retains upstream license/readme.
Existing valid installs are reused and incomplete installs preserved. Package
includes the setup script. Actual download/hash verification, repeat setup and
incomplete-install preservation passed. FFmpeg 9 removed filter_complex_script
and stalled at visual-input EOF; direct filter arguments and padded visualization
branches fix both, with song duration bounding output and soundtrack unchanged.
Actual 10.84-second exports passed with downloaded 9.0.2 (2,316,619 bytes) and
existing 4.2.3 (2,172,797 bytes). Private-folder discovery worked without an override;
the new video decoded and its title/scopes/FFT frame was inspected.
Windows desktop build, SID dummy UI test and 20 Python tooling/package tests
passed. Package fixtures now include the MIT license added in the prior commit.
CI was not due. Evidence: `.local/sid/ffmpeg*`. No physical UI/network-cancel or
listening qualification claimed. Next: macOS Intel/Apple Silicon and Linux release preparation.

License decision, 2026-10-30: owner selected MIT for project code while preserving
dependency licenses. LICENSE and public/package documentation now reflect it.
Explicit GPL SID helper sources retain GPL-2.0-or-later. The clean `b72c9a4`
84-file candidate passed relocation, exports, dummy launch, helper RPC and all
manifest hashes; final ZIP refresh adds the MIT license and current docs.

Release-candidate follow-up: implementation `d2f435d` is pushed. Its clean
82-file package passed all manifest hashes, Unicode/space relocation with a
restricted PATH, SID/PRG/WAV/MP4 export, dummy editor launch and live helper RPC;
installation hashes were unchanged. Archive was 17,693,176 bytes. Report:
`.local/sid/release-package-report.json`. Helpers were subsequently rebuilt from
the packaged shared configuration, and stream/offline comparison passed again.
A follow-up reuses output devices and compiled note players between auditions,
recovers a failed helper on retry, and adds Windows mode launchers.

Realtime/release preparation, 2026-10-30, based on `5605cf9`: SID song, loop
and note playback now use a warmed persistent external emulator and bounded
512-frame PCM blocks. Guitar playback and fret/click auditions use NativeRenderer
on a bounded producer thread. Export and realtime SID share configuration source.
All 31 Windows desktop tests passed; native dummy tests now cover the live path.
`tools/sid_stream_verify.py` compares 520,343 frames across two warmed loads: max
3 PCM16 units difference from offline, within an explicit 4-unit tolerance.
Warm load/first-block ~12–13 ms; startup table warm-up ~327–450 ms. These exclude
compilation/device latency. Logs: `.local/sid/realtime-{build,tests}.log` and
`.local/sid/stream-verification/report.json`. No physical listening claim.
Earlier tests expected complete queued PCM and were updated to assert streamed
playback; initial comparison exposed retained emulator-state differences, now
reported explicitly. SID seeking advances preceding state, and edits stop the
running snapshot. The package uses helpers rather than the legacy 579-note atlas.
README, contributing, realtime and release guides now describe current behavior.
Full local CI passed at `.local/ci/runs/20261030T083730Z-d69d2ee3/report.json`
(22 native, 105 Python with one platform skip). Historical audit inspected 474
Git blobs with zero credential-pattern matches; this is a bounded scan, not a
guarantee. The later MIT decision supersedes the pending-license note; remaining gates are
in docs/release.md.

MP4 export, 2026-10-30, based on `85b1df4`: native More > Export MP4 Video
and CLI sid-export-mp4 use isolated SID voices, mixed FFT, fixed title/author
plate and external FFmpeg H.264/AAC fast-start encoding. Windows desktop build,
five focused native cases and four launcher tests passed. An actual 10.84-second
export decoded to 325 frames, all with title pixels; moov preceded mdat. The
bass-only intro had zero AC RMS in voices 1/2 and 2340 PCM units in voice 3.
Output: 2,169,351 bytes. Evidence under `.local/sid/`: video-test.mp4,
video-test.log, video-final.png, video-build.log. Early checks caught reversed
mute documentation and transient title drawing, corrected before final checks.
No Facebook upload, listening or other-platform qualification is claimed.
FFmpeg remains external; see the SID guide. Next requested work: replace
prerendered playback/auditions with low-latency realtime synthesis throughout.

UI scale follow-up, 2026-10-30: 4K displays now default to maximized 200%.
The scale selector is a normal toolbar button with a popup in both editor modes;
the dedicated menu row is removed. Saved choices and CLI overrides still apply.
Windows desktop editor build and four focused CTest cases (SID/guitar UI plus
two generated fixtures) passed; the synthetic 4K 200% capture was inspected.
No physical display/input qualification is claimed. The first due CI attempt
passed 22 native and 105 Python tests (one POSIX-only skip), but correctly rejected
its receipt because source changed while it ran. The stable-source rerun passed;
its log is `.local/sid/scale-toolbar-ci.log` (before this documentation update).
Tested source: the working change based on `bea19f9`, Windows 11 / Clang 18.1.8.
Restart through `bun run dev` to use the rebuilt editor.

4K UI follow-up, 2026-10-29: the editor now uses separately rasterized 16/24/32/48/64
pixel fonts and matching style/layout sizes. A 4K monitor defaults to maximized
400%; high-DPI desktop scaling otherwise selects 200%. The top UI scale menu
supports 100/150/200/300/400% and persists its choice in `ui-scale.txt` beside user
data. `bun run dev --ui-scale 4` forces 400% for the launch. SID columns/rows and
fixed control widths scale with text; changing scale resets default column widths
for that scale rather than retaining widths from a different magnification.
The user's already-running editor was left intact. Updated objects/libraries and
UI tests were built, then linked as `.local/build/desktop/bin/gtracker_editor-scale-preview.exe`
for verification. The usual editor target will relink on the next `bun run dev`
after the user saves/closes the current window. Synthetic 4K 400/200% captures
were inspected under `.local/build/desktop/editor-sid-4k-{400,200}.bmp`;
this is distinct from the user's actual display/input check. No audio or song
format changes are involved.
Focused native UI checks passed (four cases including their two generated
fixtures), as did four development-launcher tests. Logs:
`.local/sid/scale-verify-{build,tests}.log`. The independent preview also passed
a dummy-device native launch; no running user process was terminated/replaced.

Development launcher follow-up, 2026-10-29: `bun run dev` now configures/builds
the desktop editor/CLI and launches a new SID song straight from this checkout;
`--guitar` or a score path chooses another start. `python tools/dev.py dev` is
the equivalent without Bun. No JS dependencies/install or package extraction
is involved. Repo resources are used directly; development data uses `.local/dev`.
Existing SID tools are checked; missing tools are prepared on Windows, requiring
the documented backend build prerequisites. Failed setup/build/checks stop launch.
Actual bounded SID launch and dummy guitar smoke passed on Windows; launcher
failure/default/path tests cover rejecting stale builds and backend failures.
Logs are `.local/sid/dev-{launcher,guitar-launcher}.log`. An initial mock test
lacked its built-executable fixture and was corrected before the passing checks.
This change adds no watcher/hot reload and no physical listening qualification.

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
