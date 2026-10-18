# Analysis worker protocol, version 1

`analyze.py` is a **metadata-only baseline** for the recording-to-pattern
analyzer. It does not infer pitches, onsets, strings/frets, articulations, or body
percussion. It has no third-party Python dependency and downloads no model weights.

The separate, explicit [`propose.py` experiment](../docs/proposals.md) can generate
single-note candidates and pitch-compatible fingering alternatives for native
review/undo. It uses a different versioned sidecar; this worker's v1 protocol and
unavailable transcription capability remain unchanged. Chords and articulations
are unsupported, and real-guitar recognition has not been qualified.

## Invoke one job

```sh
python analysis/analyze.py --input .local/take.wav --output .local/take.analysis.json --job-id take-001 --pattern-revision 7 --bpm 90
```

The input must be a nonempty, uncompressed mono/stereo integer-PCM WAV with
8-, 16-, 24-, or 32-bit samples. Floating-point WAV, compressed audio, and other
containers are not accepted by this baseline. Original audio is preserved. The
worker checks for truncation and rejects a file that changes while it is running.
It writes the output JSON atomically and exits zero only after writing a result.
Argument errors exit 2; input/analysis/output failures exit 1. The result file is
the machine-readable interface; stdout is a short human-readable completion line.

For a preserved WAV/pattern/timing bundle, use [take import](../docs/takes.md).
That wrapper retains this job result unchanged and adds an explicit source-frame
to-beat mapping in a separate v1 take manifest. Selection in that manifest does
not imply that this metadata-only worker analyzed a cropped region; its result
still describes the original whole-file audio and has no note transcript.

## Result contract

| Field | Meaning |
|---|---|
| `schema`, `version` | `gtracker.analysis-result`, integer `1` |
| `job_id` | Opaque caller ID, echoed unchanged |
| `source_pattern_revision` | Caller revision, echoed unchanged; use it to reject stale imports |
| `status` | Currently always `metadata_only` |
| `capabilities` | Explicit implemented/unimplemented capabilities |
| `source` | Input filename, SHA-256, byte size, PCM format, sample rate, channels, frames, duration |
| `timing` | Original source sample coordinates, half-open whole-file selection, BPM hint |
| `diagnostics` | Normalized aggregate/per-channel peak and RMS |
| `events` | Currently always `[]`; no transcript is available |
| `limitations` | Explicit description of what was not inferred |
| `provenance` | Engine name/version, Python version, UTC completion time |

Signed integer amplitudes are divided by `2^(bits-1)`. Eight-bit PCM is unsigned,
so 128 is subtracted first. A peak of 1 represents the negative full-scale integer
amplitude; positive full scale is one quantization step below 1. Aggregate RMS is
over all interleaved samples; channel RMS is over frames for that channel. These
are signal metrics and do not estimate perceived loudness or quality.

Timing remains in the input's original sample coordinate system. `bpm_hint` is
caller-provided context, not a detected tempo. The selection is currently the
whole file. Short takes are the primary intended workflow, and future selection,
pre-roll/tail, and long-recording chunking can extend the versioned job interface.

An empty event array here means **transcription has not been implemented**. It
must not clear an existing pattern or be presented as proof that a take has no
notes. The current editor does not invoke or import this worker automatically.
Optional single-note sidecars retain uncertainty and enter the document through
an explicit undoable operation only when their full source score snapshot still
matches; see [the proposal workflow](../docs/proposals.md).

## Research environment

When implementing an actual recognizer, create a separate environment under
`.local/` and select tested versions of NumPy/SciPy, PyTorch, and the particular
research models needed by that experiment. This bootstrap does not install them
or publish a speculative dependency lock. Record versions, model provenance,
input hashes, evaluation data, and the capabilities each experiment really adds.

The intended progression is a useful note/onset proposal, guitar-specific string
and gesture hypotheses, and fitting against the same C++ renderer used by the
editor/game. Native Python bindings can be added when repeated render calls make
a process-per-render interface inconvenient. A native binding alone does not
provide gradients through the physical simulation.
