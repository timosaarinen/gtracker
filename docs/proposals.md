# Optional single-note proposals

The default analysis worker remains **metadata_only**. An explicit research
adapter now proposes coarse notes from a short, single-note line in one selected
source channel. It does not solve general guitar transcription. Chords,
overlapping ringing strings, fast ornamentation and noise can produce missed,
split or wrong notes, including octave errors. No real-guitar result or listening
verdict has been recorded for this baseline.

## Try the original two-bar fixture

Build the CLI, then generate a preserved take from the original native
`examples/monophonic-study.gtrk.json` and run the actual proposal adapter:

```sh
python tools/dev.py build --profile headless
python tests/make_editor_proposal.py --output .local/proposal-demo/take --profile headless
python tools/dev.py editor --take .local/proposal-demo/take/take.json
```

In the editor, open **Note proposals** and load
`.local/proposal-demo/take/proposals.json`. Expand each note, review its source
beat, pitch diagnostics and possible string/fret assignments. Every note starts
as **Skip this note**. Choose the fingerings you want, set an authoring strength,
then **Accept selected plucks**. The whole batch is one undo step. Render and A/B
with the source; save/reopen retains the take and accepted normal pattern events.
The original source PCM and its metadata-only analysis result are unchanged.

The four-note fixture currently yields four near-pitch candidates. Its higher
notes have two, three and four possible string assignments. Pitch alone cannot
recover the actual played string; the test explicitly chooses the authored
sixth string. Generated fixture success does not qualify a microphone, acoustic
guitar, chord, articulation or general recognition accuracy.

A broader generated check on the same Windows i7-10700K/Clang18 Release machine
returned four candidates at 16 and 44.1 kHz, but five at 48 kHz: one attack first
appeared an octave low before its stable estimate. Stereo 24-bit and an added
seeded-noise case retained that false positive. The unsupported chord probe
returned misleading single-note candidates; silence returned none. These are
documented failure probes, not evidence of polyphonic recognition. Full inputs,
labels, matching tolerances, engine parameters and timings are in ignored
`.local/nightshift/proposal-evaluation-eb243d7b/report.json`, bound to its recorded
source fingerprint. That run's whole-job times were roughly 0.79–1.17 seconds for
the 4.5-second non-silent cases; they include source verification and native
pattern validation and are not pure pitch-kernel timings.

## Analyze a selected local take

First use [take import](takes.md) or [record-take](capture.md) to preserve a WAV
and its source-sample alignment. Set the take's selection to at most 30 seconds.
Save current pattern edits before submitting a proposal job.

```sh
python analysis/propose.py --take .local/my-take/take.json --channel 1 --assume-monophonic --output .local/my-take/job-001.proposals.json --json
.local/build/headless/bin/gtracker_cli proposal-info .local/my-take/take.json --proposals .local/my-take/job-001.proposals.json --json
```

The single-note assumption and one-based channel are required. Stereo is never
silently mixed down. The adapter reads the selected channel into a bounded
double-precision analysis copy; it preserves the original PCM width, channels,
rate and bytes. Output belongs under ignored `.local/`; choose a new job filename
for each run. No weights, third-party package or network request is needed.

The original implementation uses the difference function, cumulative-mean
normalization, first qualified trough and parabolic period refinement described
by de Cheveigne and Kawahara's [YIN paper](https://www.audiolabs-erlangen.de/content/05_fau/professor/00_mueller/02_teaching/2024s-sarntal/01_group_F0/2002_CheveigneK_YIN_JASA.pdf).
It is a limited YIN-style experiment, not their reference implementation or a
reproduction of the paper's evaluation. It searches 65–1,000 Hz without knowing
the expected score pitches. A short box average reduces analysis rate toward
8 kHz; this is not high-quality anti-alias filtering. Energy gating, periodicity,
rough pitch continuity and a minimum eight voiced frames form stable regions.

Onsets use a coarse energy rise followed by an original-rate amplitude crossing.
The recorded 100 ms uncertainty scale is a review aid, not a calibrated confidence
interval or guaranteed error bound. No tempo/grid quantization or latency
calibration occurs. Pitch spread, periodicity and voiced fraction are separate
diagnostics, not calibrated probabilities. End frames delimit stable regions;
they do not detect physical release. Acceptance adds **plucks only**, so manually
author damping, durations and expression as needed. Strength defaults to 0.6 as
an explicit authoring choice; it is not inferred from recording amplitude.

## Job and acceptance contract

`gtracker.note-proposals` v1 is a standalone optional sidecar. The v1 take and
metadata worker formats stay intact. The result includes a unique job ID, take
ID, original source hash/format/channel, full alignment, source score snapshot
and revision, engine version/code hash, parameters and limitations. The code hash
covers `monophonic.py`, `propose.py`, `analyze.py` and `tools/takes.py` in that order.
See [the schema](../schema/note-proposals.schema.json).

The native reader checks identity, bounded data, strict versions/fields and
pitch-compatible alternatives. Acceptance also compares the entire score
snapshot, including revision. Even a same-revision external edit is stale.
The editor rechecks the on-disk source/take before accepting. A successful edit,
undo or redo advances the score revision, so the old result cannot be accepted
again. Failed or late jobs do not replace newer edits. Existing events/curves
remain; a conflicting curve or an onset outside the pattern rejects the whole
batch. Source selection/context is never silently clamped to fit a score.

CLI acceptance uses an explicit [selection file](../schema/proposal-selection.schema.json):

```json
{
  "schema": "gtracker.proposal-selection", "version": 1,
  "job_id": "the-reviewed-proposal-job-id", "strength": 0.6,
  "choices": [{"note_id": "note-1", "alternative": 1}]
}
```

```sh
.local/build/headless/bin/gtracker_cli accept-proposals .local/my-take/take.json --proposals .local/my-take/job-001.proposals.json --selection .local/selection.json --output .local/reviewed.gtrk.json --json
```

Alternative indices are one-based and refer to the reviewed list. The CLI writes
a separate normal pattern with an advanced revision and refuses to overwrite
source/take/job/selection inputs. In the editor, acceptance uses normal undo/redo;
Save writes the attached take's editable pattern. `no_confident_candidates`
means this implemented experiment found no qualifying region, not that the
recording is silent. This differs from the metadata worker's unavailable
transcription capability.

## Repeatable generated evaluation

From a source checkout, after building the native CLI, run:

```sh
python -m analysis.evaluate --output .local/evaluations/proposals-001 --json
```

Use a new ignored directory each time; existing output is never merged or
removed. `--profile desktop` selects that CLI instead of the default headless
build. No model downloads or third-party Python packages are required. The tool
renders the original `monophonic-study.gtrk.json` and evaluates clean16/44.1/48kHz,
duplicated-channel stereo24-bit, seeded Gaussian noise (standard deviation0.003),
silence and an explicitly unsupported two-string chord. The stereo case copies
one mono channel twice; it is not a real stereo capture or independent channel
test. All PCM, preserved bundles, raw jobs, selections and results stay local.

`report.json` uses `gtracker.proposal-evaluation` v1 and records source before/
after, renderer/engine identities, interpreter/platform, source hashes, labels,
conditions and command/job timing. Compatible pitch/onset matches are unique,
nearest in authored-label order, within50 cents and120ms. Precision/recall and
false/missed counts describe these generated notes; known-fingering coverage in
the alternatives is separate from detected-string accuracy. Silence has undefined
precision/recall, and unsupported chords have no accuracy metric. A completed
experiment means the workflow ran, not that proposal quality passed. Failures
retain a partial report and their owned generated files for inspection.

The current experiment finds four matching notes at16/44.1kHz; 48kHz, stereo24 and
the noise case each retain an extra octave-low attack candidate (precision0.8,
recall1 under these tolerances). Raw jobs remain unchanged. The report's optional
**oracle demonstration** uses known authored labels to select four candidates,
reject extras, restore pitch/onsets/strength and add the original damping. Its
separate score/WAV demonstrate native acceptance and correction round-trips;
those corrections are neither inferred nor human-reviewed and are excluded from
accuracy metrics. The report records whether that known-label reconstruction is
byte-identical to the authored source. Real takes need separate consented data,
labels and listening evaluation.

## Research environment and candidates

Use a separate optional environment: `python -m venv .local/venvs/analysis`.
This baseline uses only Python 3.10+ standard-library modules. Native tools/core
do not import Python or depend on this environment. Tests generate their audio:
`python -m unittest tests.test_proposals -v`. Record source revision, engine code
hash, interpreter, inputs, labels and failures with each experiment. Original
scores belong in Git; recordings, evaluation output and weights stay `.local/`.

The following upstream sources were inspected on 2026-10-18; neither model was
installed or benchmarked, and no model/dataset terms are inferred from a code
license alone:

| Candidate | Inspected source and adoption work remaining |
| --- | --- |
| Basic Pitch | [Commit fa5997a](https://github.com/spotify/basic-pitch/tree/fa5997af0a8210982619003269994a1be25eddf3), code version 0.4.0, [Apache-2.0 license](https://github.com/spotify/basic-pitch/blob/fa5997af0a8210982619003269994a1be25eddf3/LICENSE). Its [README](https://github.com/spotify/basic-pitch/blob/fa5997af0a8210982619003269994a1be25eddf3/README.md) describes polyphonic note/bend output and mono/22.05 kHz analysis. The [dependency file](https://github.com/spotify/basic-pitch/blob/fa5997af0a8210982619003269994a1be25eddf3/pyproject.toml) selects runtimes by platform/Python version. A future adapter needs a tested compatible interpreter/runtime lock, selected weight hashes/terms, original-coordinate conversion and our local evaluation. |
| FretNet | [Commit d481054](https://github.com/cwitkowitz/guitar-transcription-continuous/tree/d481054f54184374c04b1cc27a487dc35c87f353), package 0.0.2, [MIT code license](https://github.com/cwitkowitz/guitar-transcription-continuous/blob/d481054f54184374c04b1cc27a487dc35c87f353/LICENSE.txt). The [README](https://github.com/cwitkowitz/guitar-transcription-continuous/blob/d481054f54184374c04b1cc27a487dc35c87f353/README.md) describes continuous string-level pitch and GuitarSet evaluation. Its [requirements](https://github.com/cwitkowitz/guitar-transcription-continuous/blob/d481054f54184374c04b1cc27a487dc35c87f353/requirements.txt) include an unpinned `@main` dependency. A future experiment needs dependency/weight pins, checkpoint and dataset-term review, an inference adapter and reproducible local results. |

Next research should separate pitch/onset accuracy, fingering alternatives and
technique capability, include explicitly selected real takes, and document chord,
noise and repeated-note failures. No automatic model download is part of this
workflow. Full-song overlap/reconciliation and continuous articulation proposals
remain future work.
