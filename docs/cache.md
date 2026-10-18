# Optional persistent local PCM cache

Patterns and songs can reuse complete checked renders. Caching is explicit;
without a directory the CLI writes no cache and uses the normal exporter.

```sh
python tools/dev.py render-song --cache-dir .local/render-cache --output .local/night-shift.wav --json
python tools/dev.py render-song --cache-dir .local/render-cache --output .local/night-shift.wav --json
python tools/dev.py render --cache-dir .local/render-cache --output .local/campfire-cached.wav --json
```

The first load reports `cache_status: miss`; an unchanged load reports `hit`
and skips native audio generation. Cache access selects streaming automatically.
`--refresh-cache` forces regeneration. `--cache-read-only` reuses a valid entry
but renders independently on a miss or invalid entry, without changing the
managed cache. These modes require a directory and cannot be combined.

## What identifies a result

The key includes the complete canonical pattern/song snapshot, instrument model,
tuning and parameters, performance/curve points, seed, rate, instrument tail,
every production field, override/bypass choice and exact preview frame bounds.
It conservatively includes source metadata and unused song definitions too.
Changing the processing block size or moving unchanged source files does not
change the key. Independently rendered patterns are never concatenated as a
substitute for a continuous song.

The key also includes renderer version and `renderer_build_id`. CMake computes
the build digest from native sources/headers, build/dependency recipes, declared
compiler/target information, C/C++/link flags, Lua/sanitizer settings and actual
build configuration. Source changes trigger automatic reconfiguration, including
dirty development edits. Paths are relative in the source digest; timestamps
and checkout locations are not an identity shortcut. Some unrelated native-tool
edits also invalidate conservatively. This describes declared build inputs,
not a promise of bit-identical math across every possible compiler/runtime.

`gtracker_cli --version --json` reports the compiled identity. Developer packages
record that native identity as well as source fingerprints, toolchain/options
and per-file hashes. Core-only consumers can query `gt_renderer_build_id()`;
the core gains no JSON, editor, device, Lua or Python dependency.

## Checked storage and recovery

Entries live in `DIRECTORY/gtracker-v1/KEY_PREFIX/KEY.wav` plus `KEY.json`.
Receipts follow `schema/render-cache.schema.json`, with a canonical payload
checksum and the WAV SHA-256, fixed PCM16 format/size, exact key/build/timing and
original float signal metrics. A hit checks the receipt, header and full audio
hash, then hashes the bytes again while atomically copying them to the output.
Changing PCM between validation and copying cannot publish those changed bytes.
Receipt parsing is bounded to 64 KiB and depth16; hash/copy buffers are 64 KiB.

A miss renders to an owned sibling stage beside the requested output, checks
that completed audio, and stores a complete cache WAV plus receipt. The receipt
is published last. It then atomically publishes the caller's output. A cache
write failure still permits the complete output and reports `store_failed` with
the reason. Missing, stale, truncated or mismatched entries regenerate; writable
recovery reports `rebuilt`. Other keys and unrelated files are not removed.

Cancellation during generation, hashing or copying keeps the previous output
and removes handled temporary work. A complete checked entry may already exist
if cancellation arrived after cache storage but before output publication;
that entry remains valid for reuse. Partial files without a valid receipt cannot
be hits. A forced process kill can leave ignored owned stages/temporaries, which
are never looked up as completed entries. There is no automatic eviction or
unrequested cache deletion. Caches and renders belong under ignored `.local/`.

## Honest measurements

On a hit, `render_ms`, `warmup_frames` and the export float `audio_buffer_bytes`
are zero because no new generation/warm-up/export block ran. `crop_start_frame`
still identifies the selected region. `source_render_ms` records the checked
entry's original generation time; it is not a claim of current work. Peak/RMS,
ending peak and full-scale counts are marked `checked_cached_float_receipt`.
Current lookup/store costs and total export elapsed time are reported separately.
The renderer/schedule, hash/copy/encoder buffers and process overhead still use
memory, so a zero export buffer is not a zero-memory claim.

`--progress-json` adds `cache_progress` JSON lines on stderr for lookup, copy,
hash and store phases. Native generation retains its separate frame progress.
Final JSON stays on stdout. Source-score size, PCM size, signal checks, elapsed
cost and listening observations remain distinct evidence.

Generated native/CLI checks cover exact hits, changed audible inputs, dirty-build
identity, previews, corruption, incomplete entries, read-only modes, unavailable
storage, a mutation between lookup and copying, and cancellation/failure during
cache phases. Physical playback and listening are still separate qualification.
