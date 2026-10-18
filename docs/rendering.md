# Bounded WAV export and previews

The normal editor/CLI pattern render retains audio in memory for playback and
keeps its 32 Mi-float-sample limit. `--stream` uses the same native renderer and
writes completed blocks to a sibling temporary WAV, then atomically publishes
the finished file. It does not allocate the whole rendered audio buffer.

The editor's in-memory preview also supports **Cancel render** and frame-based
audio progress, followed by waveform preparation. A cancelled or obsolete job
retains the last completed preview; it never publishes partially generated audio
or starts pending playback. Edits and shutdown request cancellation. The native
`RenderControl` callbacks on `render_pattern()` run on its calling thread, with
checks before allocation, at 1,024-frame boundaries and during metrics. The editor
also checks during waveform preparation. Callbacks may throw; they do not enter the audible render/cache identity.
This is cooperative job control, not a real-time interruption guarantee. The
32 Mi-float-sample preview limit still applies; song export remains streamed.

```sh
python tools/dev.py render examples/slide-study.gtrk.json --stream --production examples/room.json --output .local/slide-stream.wav --json
python tools/dev.py render examples/slide-study.gtrk.json --production examples/room.json --start-frame 48000 --end-frame 144000 --output .local/slide-preview.wav --json
```

Frame ranges are zero-based, half-open output frames at the selected sample
rate, across all channels. Supplying crop/block/progress/cancellation options
automatically selects streaming. A preview warms up the entire instrument,
contact and room state from frame zero before discarding that prefix. It is an
exact PCM crop of the complete render, without resetting at the preview start
or automatically appending a new tail at the crop end. Large starting positions
therefore require real warm-up work; there are no state checkpoints yet.

The JSON report includes output `frames`, original `timeline_frames`,
`warmup_frames`, `output_reaches_timeline_end`, peak/RMS/full-scale counts and
the ending 50 ms peak. `production_tail_seconds` describes the planned complete
performance; it is not an extra tail added after a crop. Full streamed and
in-memory output share the same render identity and WAV bytes. A crop derives
a versioned identity including both exact frame bounds. Processing block size
changes neither identity nor sound.

`--block-frames` is bounded to 1–65,536 (default 4,096). The report's
`audio_buffer_bytes` measures that interleaved float block only; fixed native
delay memory, schedules, encoder buffers and process overhead are additional.
It is not a process memory measurement. `render_ms` counts native generation,
including any warm-up; `export_elapsed_ms` also includes metrics, PCM encoding
and file I/O, but excludes score compilation/renderer construction.

## Progress and interruption

`--progress-json` writes throttled JSON-line frame progress to stderr, leaving
the final `--json` result on stdout. The developer wrapper preserves that split.
Optional [persistent caching](cache.md) adds byte-progress phases and can skip
generation/warm-up altogether on a checked hit; `crop_start_frame` remains the
requested source-timeline position even then.
Progress reaches the processing endpoint before publication; only the final
successful report certifies a completed output file.

`--cancel-file PATH` requests cancellation when that file exists. The native CLI
also handles SIGINT/Ctrl+C during streamed work. The developer wrapper requests
graceful cancellation through an owned `.local/` marker when interrupted and
waits up to ten seconds for its child. A second interruption can stop it harder.
No background process or scheduler is installed.

Cancellation returns code **130**, JSON `status: cancelled`, and retains any
previous completed output. Errors return code **2** in the native CLI. Owned
temporary WAVs are removed on handled failures; forcibly killing the process can
leave a temporary file, never a completed partial WAV. A caller-selected cancel
marker is not automatically deleted. Invalid ranges/block sizes, callback
failures, disk-write failures and RIFF-size overflow cannot replace old output.

Classic PCM RIFF remains bounded below 4 GiB per file. Too-large output fails
before audio generation; select a preview or lower rate. RF64 and arbitrary
long-duration container formats are future work. Streaming removes the audio
buffer limit; it does not remove native schedule or duration bounds.

The native API is `export_wav()` in `include/gtracker/export.hpp`, accepting a
fresh/reset `NativeRenderer`, exact frame crop and progress/cancel callbacks.
It performs no device I/O. Generated tests compare dry, slide and stereo-room
exports, cropped PCM, varied blocks, monotonic progress, handled cancellation,
failure preservation and oversized RIFF rejection. POSIX SIGINT has its own
test; Windows physical console Ctrl+C remains a separate qualification.
