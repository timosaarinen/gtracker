# Native SID tracker

Start a blank SID song with `python tools/dev.py editor --sid`, or open
`examples/sid/heavy-mellow.sidtrk.json`. The guitar screen has **New SID Song**;
loading a `.sidtrk.json` selects SID mode automatically. **Guitar mode** returns
to the preserved guitar document. The desktop ZIP includes the backend and
realtime helper. Nothing is downloaded during note entry.

## Enter and arrange music

A blank song starts in Edit mode, octave 4, Heavy Lead, step 1, speed 6, with an
empty pattern in each HEAVY/MELLOW section. Click a NOTE cell and play
`Z S X D C V G B H N J M`; `Q 2 W 3 E R 5 T 6 Y 7 U` is the octave above.
Scientific C4 is MIDI 60; supported pitches are C1–B6. Finnish QWERTY uses these
same letter/number positions. Every note writes the selected preset and advances
by step 0/1/2/4. Edit off auditions without changing music or moving the cursor.

A enters OFF. Delete clears the event/block; absent events are HOLD and retain
voice state. All three physical voices advance together through 64-row patterns.
Drums store canonical C4 and use fixed preset pitches. Palette clicks choose the
entry preset; **Apply to Selection** changes existing notes as one undo operation.
Digits in INSTR change existing notes' presets; digits in NOTE are piano keys.
Numpad divide/multiply change octave and minus/plus select presets, with visible
alternatives. OS note-key repeat is suppressed.

Arrows navigate cells; Tab/Shift+Tab change voice. Home/End go to first/last row;
Page Up/Down move 16 rows. Shift+Arrows or mouse drag select blocks. Ctrl/Cmd+C/X/V
copy/cut/paste rectangular events, bounded to this pattern. Excess rows/voices
are omitted. Transpose buttons move pitched notes by semitone/octave and reject
out-of-range changes; drums stay fixed. Ctrl/Cmd+Z, Shift+Ctrl/Cmd+Z and Ctrl+Y
undo/redo. Escape toggles Edit outside dialogs; dialogs own Escape first. Text
fields own their keys; title/author changes apply on Enter.

New Pattern creates and places a pattern. Repeat places another reference;
editing it affects every use. Duplicate makes an independent variation.
The library shows **Used N times**, rename, placement and unused deletion.
Up/Down move entries within a section; Move to changes section. Remove from Song
retains the pattern. Section removal explicitly offers moving or removing entries
while retaining patterns. Section names never change sound or tempo.

Ctrl/Cmd+S saves, Shift+Ctrl/Cmd+S opens Save As, Ctrl/Cmd+O opens a SID source.
Source JSON preserves Unicode names, stable IDs, bank version and all music.
New/Open/close/mode switches protect unsaved work. Recovery writes unique score
copies every five seconds while changed/unsaved. **Review recovery copies** opens
another session's copy for review and Save As; it does not restore cursor, undo
or transport. Failures remain visible. Resources resolve beside the executable;
data/cache/recovery use application data or an explicit `--data-dir PATH`.

## Transport and export

F5 plays from the beginning; F7 or Space starts at the selected occurrence/row.
Space then pauses/resumes. F8 stops and cancels pending autoplay. A persistent
emulator supplies small PCM blocks; no complete WAV is prepared before playing.
Seeking advances the real preceding SID state on a worker. Musical edits stop
the current snapshot and invalidate its compiled SID. Follow tracks occurrences;
navigation disables it so it cannot fight intentional movement.

F6 continuously loops the selected pattern in the C64 player. A leading HOLD is
silent on the cold first pass and can carry a voice on later passes. Oscillator
and envelope state continues across repeats, without splicing recorded PCM.

The playhead uses submitted samples minus queued application audio;
physical output latency is a separate measurement. PAL uses 985248 cycles/s and
19656 cycles/frame. Speed 4–12 is global, with four rows per beat; default 6 is
~125.31 BPM. Register traces locate first gate at play call `speed + 2`, first
pitched tick at `speed + 3`. Timeline/seek use the latter reference. Output WAV
contains initialization, the authored duration and exactly three release seconds.

**Export SID** produces one-subtune PSID v2, explicit PAL/6581 and VBI timing.
More exports MP4 Video, WAV, C64 Program or diagnostic GoatTracker SNG. The PRG has BASIC
`10 SYS2061`, an IRQ launcher at $080d, and the same music payload at $1000.
It masks CIA interrupts, maps RAM/I/O explicitly, preserves CPU registers and
acknowledges a VIC raster IRQ once per frame. Music must fit below $a000.
An OFF terminal followed by silent HOLD data gives a finite ending. Empty sources
may be saved; completed export requires arranged NOTE events. All final writes
are atomic; export dialogs confirm existing targets and protect source aliases.

SNG/PSID text uses printable ASCII, one `?` per unsupported code point, and a
31-character limit, reporting conversions without damaging source names.
Bounds are 64 patterns, 128 placements, 16 sections and 1 MiB. Native validation
checks schema/bank versions, references, unique IDs/events and expanded pitches.
Preview rejects over 32 million mono samples before allocation, never silently
shortening the song. Revision values currently have an explicit 1..1000000 bound.

## Bank and reproducibility

Original bank 1 lives in `src/sid/compiler.cpp`, with stable names/roles in
`src/sid/song.cpp`: heavy pulse lead/bass, bright major/minor arps, warm triangle
lead, soft bass/major/minor arps, kick, snare and hat. Notes initialize pulse,
wave and vibrato state. The global filter is unrouted and volume is fixed.
Bank content is hashed in every export; future released sound changes must use
a new bank version. The compiler retains source pattern reuse, deduplicates
equal backend voices and checks backend capacities. GoatTracker 2.77 provides
the proven packer/player. Its unused instrument-63 mechanism initializes tempo
before the first note.

The external `gtracker_sidrender` adapter uses pinned libsidplayfp 3.1.1 and
libresidfp 1.2.3, fixed PAL/6581, delay 0, 48 kHz mono PCM16 and interpolating
resampling. It loads no ROMs, global configuration or song-length database.
The first inspected sidplayfp 3.2.1 console binary differed by up to four PCM
units between runs. Serializing reSIDfp's analogue table-construction tasks
removed that nondeterminism while retaining the table algorithms. This external
adapter is a deliberate deviation from the plan's console invocation; it also
avoids console configuration writes and fractional-duration rounding. It adds
no custom SID synthesis and stays separate from the guitar core/application.

Auditions use actual pitches through the same player/renderer, a short gate,
OFF and release. Auditions stream directly from the warmed emulator. The legacy prepared atlas remains optional compatibility material. Atlas identity binds bank,
compiler/configuration and backend hashes. Full-song cache additionally binds
canonical source, exact SID, native build and duration; complete PCM hashes are
checked and corrupt entries regenerate. Jobs own their processes and discard
stale/cancelled autoplay after editing, loading or undoing.

Windows developer setup requires MSYS2 i686 SDL/GCC and UCRT64 GCC/autotools.
Linux prerequisites and native helper setup are in the
[Linux guide](https://github.com/timosaarinen/gtracker/blob/main/docs/linux.md).

```sh
python tools/sid_backend.py prepare
python tools/sid_backend.py check
python tools/dev.py build --profile desktop
python tools/dev.py sid-export examples/sid/heavy-mellow.sidtrk.json --output .local/song.sid --json
python tools/dev.py sid-render examples/sid/heavy-mellow.sidtrk.json --output .local/song.wav --json
python tools/dev.py sid-export-prg examples/sid/heavy-mellow.sidtrk.json --output .local/song.prg --json
python tools/dev.py sid-export-sng examples/sid/heavy-mellow.sidtrk.json --output .local/song.sng --json
python tools/dev.py package --profile desktop
```

`sid-info` and `sid-validate` inspect sources. Native export commands accept
`--sid-tools DIR`; packaged CLI resolves adjacent tools. The package carries
checksums, licenses, source archives and build/patch recipe. To reconstruct
backend build inputs, copy all helper `.cpp`/`.hpp` sources and `sid_backend.py` from `sid-tools/source`
to `ROOT/tools/` and archives to `ROOT/.local/sid/`, then run the recipe with the
documented build prerequisites. GPL backend executables remain external; this
does not change gtracker's MIT license or the helpers' GPL terms.

## Demonstration and evidence

Heavy and Mellow is generated original demonstration material, not a personally
performed Timo recording. Seven editable phrases form sixteen placements: intro,
heavy theme/answer, warm minor/major contrast, a developed heavy return and a
finite outro, about 123 seconds at speed 6. Bass and drums share a physical voice.

`tools/sid_verify.py` tests real export, repeatability, 6502 registers at speeds
4/6/12, A4 pitch, all presets/voices, HOLD and released ending gates. py65 is an
authoring test dependency (development install: `.local/sid/python`). Native UI
tests cover editing, persistence, modal focus, streamed transport and stale
jobs using synthetic input/dummy devices. Tail diagnostics measure last-half-
second DC and DC-removed RMS, with a 0.005 (-46 dBFS) RMS limit; chip noise is not
a stuck voice. Physical listening, real input and hardware remain separate from
signal/register/dummy evidence. The handoff records actual checks and limits.

## MP4 sharing export

Choose **More > Export MP4 Video**. Background export supports cancellation and
atomic replacement. Output is 1080p/30 fps H.264/yuv420p, AAC 160 kb/s stereo,
with fast-start metadata. A permanent title/author plate overlays three isolated
SID voice scopes and a full-screen logarithmic FFT, bass left and treble right.
The soundtrack is the normal SID mix with initialization and release tail.
Visual high-pass/gain does not alter audio. Long titles wrap; glyph coverage
uses the installed font.

On Windows, choose **More > Set up MP4 export**, then **Download FFmpeg**.
The cancellable background helper downloads the pinned Gyan FFmpeg 9.0.2
essentials build (about 110 MB), checks SHA-256, and installs the executable,
license and upstream readme under `tools/ffmpeg` in your gtracker data folder.
It needs no admin rights and leaves PATH unchanged. An interrupted download can
be retried; an existing changed/incomplete installation is preserved and reported.
Development uses `.local/dev`; packaged Windows defaults to
`%APPDATA%/LustOfWar/gtracker`. `--data-dir` selects another location.

Alternatively, FFmpeg (libx264, AAC, drawtext, showwaves, showfreqs) can be on PATH,
in sid-tools, or selected with GTRACKER_FFMPEG. The explicit override takes priority,
then the downloaded copy, sid-tools and PATH. Other platforms use manual setup.
FFmpeg is not redistributed in the developer ZIP. Builds come from
[Gyan](https://www.gyan.dev/ffmpeg/builds/), linked by the
[FFmpeg download page](https://ffmpeg.org/download.html).
GTRACKER_VIDEO_FONT optionally selects a Unicode TTF/OTF font. Windows defaults
to Segoe UI. Text files with expansion disabled keep song text out of filter code.
No shell, account access or automatic upload is involved.

`bun run dev` prepares the separate voice helper if missing on Windows/Linux. Manual setup:

```sh
python tools/sid_backend.py prepare-video
python tools/dev.py sid-export-mp4 examples/sid/heavy-mellow.sidtrk.json --output .local/song.mp4 --json
```

The helper uses the existing pinned SID libraries without invalidating the
playback audition cache. Packaging includes the helper. Jobs retain PCM/filter
files and logs under the configured cache directory. Encoding is CPU-based.
