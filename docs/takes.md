# Preserved takes and source timing

The import command attaches a stable integer-PCM WAV to an ignored take bundle.
Use [record-take](capture.md) for explicit native capture followed by this import
workflow. Analysis remains metadata-only and recognizes no musical events.
Optional [single-note research proposals](proposals.md) use a separate explicit
sidecar and reviewed native acceptance; the preserved metadata result stays intact.
The source is copied byte-for-byte, including its header/extra chunks, channel
count, rate, pre-roll and decay tail. Selecting a region only stores coordinates;
it never crops, resamples, normalizes or replaces the source.

Build the headless profile, then try the original synthetic render:

```sh
python tools/dev.py import-take .local/demo.wav --pattern examples/campfire.gtrk.json --output .local/takes/demo --zero-frame 0 --json
python tools/dev.py take-info .local/takes/demo/take.json --json
```

Choose a new output directory for each import. An existing bundle is never
overwritten. The folder contains `source.wav`, an unchanged editable copy of the
supplied `pattern.gtrk.json`, the existing worker's `analysis.json`, and a v1
`take.json` manifest. All references are relative and contained in that folder,
so moving the whole bundle preserves the association. The original filename is
retained in metadata even though the preserved copy is named `source.wav`.

The native CLI validates the pattern snapshot. Analysis uses its revision and
an explicit job ID/source hash; it remains `metadata_only` with no note events.
The supplied pattern's authored events are retained. Empty analysis events do
not erase notes or claim that the source is silent. `take-info` verifies source
hash/format and analysis identity and reports whether the saved pattern revision
still matches the job. A newer pattern can be inspected with a clearly stale job.

## Mapping

`--zero-frame N` is the nominal source frame corresponding to pattern beat zero.
With sample rate `R`, source tempo `B` and input offset `L`, the mapping is:

```text
beat = (source_frame - zero_frame - L) / R * B / 60
```

Positive `--input-offset-frames L` means the input arrived late, so that delay is
subtracted before musical mapping. Supplying an offset labels it **manual**;
omitting it stores zero as **uncalibrated**. Neither value is a measured latency.
No loopback/clock alignment or sample-rate-conversion calibration is implied.
Fractional beat positions remain fractional; no snap/quantization is applied.

Source tempo defaults to the supplied pattern tempo. `--bpm B` overrides this
timing hint without modifying the pattern or stretching audio. Stored source
timing remains independent of later pattern-tempo edits. The original pattern
length is retained as context. The file can include negative-beat pre-roll and
positive-beat tail beyond that length.

`--selection-start-frame A --selection-end-frame Z` selects `[A,Z)` in original
source frames. The default is the whole file. `--job-id ID` sets an explicit ID;
otherwise one is generated. `--profile desktop` selects that native validator
when using a desktop build. JSON output contains no command-prefix noise.

## Evidence and limits

The v1 format is described in `schema/take.schema.json`; `tools/takes.py` also
checks cross-field bounds, contained paths and actual source/job consistency.
Supported imports are 8–192 kHz, mono/stereo, 8/16/24/32-bit integer-PCM WAVs.
Generated tests cover 44.1/48 kHz and every listed sample width. Failed imports
and cancellation remove only their own checked staging directory; cancellation
exits 130. Inputs and previous bundles stay intact.

This first step provides CLI preservation, timing and provenance. Native source
inspection is also available without Python:

```sh
.local/build/headless/bin/gtracker_cli take-info .local/takes/demo/take.json --json
```

Use `.exe` on Windows. That native reader verifies the preserved SHA-256, integer
PCM channels/rate/width, source-sample mapping, job identity and signal metadata.
Its playback-preview copy is bounded to 32 Mi interleaved float samples and a
256 MiB input WAV; the streaming Python importer/inspector can still retain
larger sources. The PCM reader also supports well-formed extensible PCM WAV,
unknown chunks and original bytes after RIFF. Preview conversion leaves the
original file unchanged. SHA-256 follows [FIPS 180-4](https://nvlpubs.nist.gov/nistpubs/fips/nist.fips.180-4.pdf);
tests check known messages and compare real take hashes/metrics with Python.

## Native comparison

```sh
python tools/dev.py editor --take .local/takes/demo/take.json
```

The take supplies its own editable pattern. The inspector's **Source take**
section also accepts a manifest path and **Open take**. Loading uses the same
unsaved-changes protection as ordinary patterns. Cancellation keeps the existing
pattern/source; failed source checks never replace them.

The waveform overlays the original source in amber and the synth in cyan on a
beat axis, with pattern-start/end markers and retained pre-roll/tail context.
**Play source** and **Play synth** share the transport. Source playback queues
its original channel count/rate; the audio device may convert to its output
format. There is no capture-clock/latency calibration implied by this conversion.
**From beat 0** starts at the mapped beat-zero frame within the stored selection;
disable it to hear selected pre-roll too. Changing that option stops playback.
All of this affects preview only; original integer PCM bytes stay unchanged.

Saving edits writes the pattern referenced by the take. Reopening the manifest
restores that pattern and the source association. A revised pattern leaves the
metadata job visibly associated with its earlier revision; no note proposal is
applied. To save a separate pattern without its take association, select
**Detach** first. Source files remain available in the original bundle.

Native tests generate a 44.1 kHz stereo fixture from the canonical renderer with
one second of pre-roll, exercise source/synth stream switching, guarded loads,
source-byte preservation and save/reopen, and capture the actual comparison UI.
They use dummy audio/video. Device selection/capture, count-in, capture tails,
measured latency calibration and editable analysis proposals remain subsequent
increments. Real-device recording and listening require separate evidence.
