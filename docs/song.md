# Continuous song arrangement v1

Renderer 0.4 accepts a small readable song document. Pattern definitions are
independent of their placements; one definition can play repeatedly with
different timing, fret transposition and excitation strength. One native
instrument and optional room instance render the complete schedule, preserving
ringing strings, held contact, body resonance and room state across placements.

```sh
python tools/dev.py validate-song examples/night-shift.gsong.json --json
python tools/dev.py render-song --output .local/night-shift.wav --json
python tools/dev.py render-song --bypass-production --output .local/night-shift-dry.wav --json
python tools/dev.py snapshot-song examples/night-shift.gsong.json --output .local/night-shift.gsong.json --json
```

The original example is 68 global beats at 96 BPM, two reusable phrases and
eight expanded placements. The complete source snapshot is about 9 KB; the
48 kHz stereo PCM is about 8.9 MB. The CLI reports both exact sizes separately.
This example is generated from authored notes/gestures, not a transcription.

## Sources and musical timing

`schema/song.schema.json` describes `gtracker.song` version 1. `patterns` defines
unique IDs with exactly one `file` or embedded `pattern`. Files are UTF-8 paths
relative to the song document, using forward slashes; absolute/drive paths and
control characters are rejected. `..` can reference a parent authoring folder.
Moving a bundle with its referenced patterns preserves the result. A snapshot
embeds exact parsed pattern data and needs none of the original pattern files.
Rendering and snapshots refuse to overwrite the song or its source patterns.

The song has one global `tempo_bpm` and excitation `seed`. These explicitly
replace the templates' tempo/seed during arrangement. `instrument_pattern`
chooses the shared model, tuning, decay, brightness and body mix; all templates
must have identical instrument settings. Per-pattern production is rejected;
place the optional production object on the song so its state stays continuous.
These choices are explicit limits of the first single-instrument arranger.

The ordered entries require an ID, pattern ID and these explicit variations:

| Field | Meaning |
| --- | --- |
| `repeat` | 1–256 consecutive occurrences |
| `time_scale` | 0.25–4 times the template's local beat duration |
| `transpose` | −24–+24 semitones applied to pluck and slide frets |
| `strength_scale` | 0–2 times pluck/body excitation; explicit damping stays unchanged |
| `gap_beats` | 0–256 global rest beats after every repetition, including the last |

Variations never modify source IDs, microtiming, points or template files.
Frets must remain in 0–36 and scaled excitation in 0–1; invalid variations fail
instead of clamping. Local beat coordinates map to absolute song frames before
rendering. Stable order resolves equal-frame events. Sample-rounded slide
collisions/conflicts and pitch limits fail clearly rather than dropping points.
`validate-song --sample-rate HZ` checks those native constraints at a chosen
rate; snapshots preserve source data without choosing a render rate.

## Streaming, tails and repetitions

Songs always use [bounded WAV export](rendering.md). The same sample-rate,
instrument tail, production override/bypass, block-size, exact-frame preview,
progress and cancellation options apply. A preview warms up from the original
song seed; it is not a fresh render of just the selected placement. Only the end
of the complete musical schedule receives the requested instrument decay tail
and room flush. No acoustic state resets at a pattern/repetition boundary.

Repeating an entry is continuous scheduled performance. It does not promise that
the final exported WAV can loop seamlessly from its ending silence to frame zero.
Infinite-loop preparation, warm-loop extraction and full acoustic checkpoints
remain future work. There is no arrangement GUI yet; the editor continues to
edit individual pattern files used by a song. Multiple instruments, tempo maps,
overlapping clips and arbitrary audio tracks are outside this first format.

The source is bounded to 8 MiB including its expanded portable snapshot, with
1–64 patterns and at most 4,096 expanded placements. Native limits remain one
million events, 16,384 slides and 65,536 points. Musical duration is at most one
day, including the chosen instrument tail at rendering. PCM RIFF has its
separate below-4-GiB output limit. These bounds constrain schedules/source data,
while PCM memory stays fixed to the selected processing block and delay state.

`song_render_identity` hashes the canonical embedded source snapshot, renderer
version, sample rate, tail, complete production settings/override and bypass.
It also includes metadata and unused definitions, so some nonaudible edits
conservatively invalidate it. Moving source files without changing content does
not change it. A preview adds exact crop bounds. Optional [persistent caching](cache.md)
also checks native build identity, complete PCM and recoverable receipts.

Native tests compare a one-pattern song with original pattern PCM and compare
notes ringing into an empty pattern with one flattened continuous reference,
both dry and wet. Tests also cover variations, copied source data, explicit
tempo/seed ownership, snapshots, moved bundles, source-write guards, previews,
invalid future data and cancelled long schedules. Physical listening remains
separate from these deterministic signal checks.
