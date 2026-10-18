# Pattern format v1

File convention: `.gtrk.json`. The native validator is the operational authority;
`schema/pattern.schema.json` is available for editors and external tooling.

The root fields are all required: `schema_version` (exactly 1), `name`, `revision`,
`tempo_bpm`, `length_beats`, `seed`, `instrument`, and `events`. Unknown fields and
event types fail clearly. This prevents a typo or unsupported articulation from
quietly changing the musical result.

## Timing and identity

- Beat zero is the start of the pattern. A beat is one quarter-note unit in this
  prototype; the display does not yet have an independent time-signature map.
- Tempo is constant per pattern, 20–400 BPM. Length is greater than zero and at
  most 256 beats. Events are inside `[0, length_beats)`.
- Event IDs are unique nonempty strings of at most 128 bytes. IDs persist across
  editing; array position is not an identity for an analyzer result.
- Revisions are positive integers up to 2^53−1 for exact exchange with common JSON
  tooling. An accepted edit increments the revision, including undo/redo.
- Equal-sample events execute in input order. Input need not be sorted; native
  compilation uses a stable sort and quantizes beats to nearest sample frames.
  A final event rounds at most to the last musical frame.

## Instrument

`tuning_midi` is a 1–12 element array in lane order, highest guitar string first.
Entries may be fractional and must be in 16–95; this is tuning metadata, not a
MIDI-file representation. Standard six-string tuning is `[64,59,55,50,45,40]`.
Alternative tunings preserve the same lane identities; the array is not sorted
automatically.

`decay_seconds` is 0.02–60, `brightness` and `body_mix` are 0–1. They describe the
current baseline and must not be interpreted as calibrated properties of wood,
string material, microphone location or a real guitar. `seed` is uint32.

## Events

| Type | Required fields besides id/type/beat | Current meaning |
| --- | --- | --- |
| `pluck` | `string`, `fret`, `strength` | Re-excite one string at a static fret |
| `damp` | `string`, `strength` | Reduce vibration and increase damping; 1 silences the string |
| `body_hit` | `strength` | Excite the illustrative body modes |

JSON string numbers start at **1**, the highest tab lane; the C ABI uses zero-based
indices. Frets are numbers in 0–36. All strengths are finite numbers in 0–1.
`body_hit` has no string or fret, and `damp` has no fret. Damping a string does not
silence a separately ringing body mode.

See `examples/campfire.gtrk.json` for a complete original pattern. A fractional
fret produces a static pitch offset; it does not represent a slide or bend curve.
Unsupported techniques are never silently reduced to plain plucks.

## Rendering and limits

The CLI accepts 8,000–192,000 Hz and 0–30 seconds of tail. The physical baseline
also requires a plucked frequency no higher than sample_rate/4. A score can thus
be valid yet require a higher rendering sample rate for its highest note.

The prototype limits a document to 1 MiB and 16,384 events, and an in-memory render
to 32 Mi float samples across all output channels. These bounds keep the first application simple.
Future arrangement/streaming work must extend whole-song use without assuming
unbounded RAM. WAV output is mono or stereo signed 16-bit PCM with clipping at
[-1,1]; the CLI reports pre-export peak/RMS and full-scale sample counts. Source
recordings are separate files. Optional v2 [production settings](production.md)
are distinct from instrument parameters and gestures; v1 remains dry.

The source is intentionally readable while gestures evolve. A future compact
encoding should preserve the declared semantics and explicit format/model version;
it should not serialize raw C structs, platform padding, Lua bytecode or Python
objects. Keep original source and uncertain analysis proposals available to the
authoring workflow while exporting only the accepted musical performance.
