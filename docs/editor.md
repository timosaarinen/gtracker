# Native editor

The first editor is a native SDL3 application with Dear ImGui controls, a custom
horizontal tab canvas, and an ImPlot display of the rendered waveform. It edits
the same pattern document and uses the same C++ renderer as `gtracker_cli`.

## Build and launch

From the repository root, with the prerequisites described in `README.md`:

```sh
python tools/dev.py configure --profile desktop
python tools/dev.py build --profile desktop
python tools/dev.py editor examples/campfire.gtrk.json
```

The direct executable is `.local/build/desktop/bin/gtracker_editor` (`.exe` on
Windows). With no pattern argument it opens a built-in eight-beat sketch and
suggests `.local/sketch.gtrk.json` as the save path. Relative pattern and script
paths resolve against the process working directory. The prototype's path fields
hold at most 1,023 bytes; longer launch paths are rejected before loading or
applying anything, so the displayed save/script path is never silently truncated.

## Editing a pattern

- **String lanes:** string 1 is the highest displayed lane. Each lane shows the
  nearest equal-tempered note name for its configured MIDI tuning. The prototype
  displays 1–12 independent strings from the document; paired twelve-string
  courses are future work.
- **Fret numbers:** a number is a `pluck` event. `x` is a `damp` event on that
  string. `+` on the amber body lane is a `body_hit` event.
- **Selection:** click an event to inspect it. The inspector edits its type,
  beat, string, fret, and strength as applicable. Beat positions are zero-based.
- **Insertion:** click empty timeline space to place the insertion cursor. The
  Snap control selects quarter-, half-, or whole-beat spacing. The `+ Pluck`,
  `+ Damp`, and `+ Body hit` buttons insert at that cursor. Double-click empty
  string/body space to insert a pluck/body hit directly.
- **Deletion:** select an event, then use `Delete event` or the Delete key.
- **Pattern settings:** edit BPM and pattern length above the canvas. Reducing
  the length past existing events is rejected; move or delete those events first.
- **Instrument settings:** expand `Instrument controls` to adjust decay,
  brightness, and body mix. Tuning is currently edited in the JSON document.
- **Undo/redo:** the buttons and Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z, or Ctrl/Cmd+Y restore
  document edits. Up to 128 prior document states are kept in memory. A Lua script
  is one document edit. Individual control updates are separate edits in this
  initial version; grouping a drag into one history operation is future work.

The path field at the top is explicit: enter a filename, then select `Load` or
`Save`. Ctrl/Cmd+S saves to that path. A `*` by the title marks a changed document.
Loading clears the current undo/redo history. Save before loading another file
or closing the window; this first version does not have an unsaved-changes dialog
or an autosave system.

## Render and listen

`Render` snapshots the current document and renders it on a native worker. The
editor remains available while that job runs. Its min/max waveform envelope is
calculated on the worker as well, with at most 2,048 display bins.

`Play` queues the current rendered mono floating-point PCM through SDL3's default
playback device. When a current render is missing, the button reads
`Render + Play`. Space performs the same action, or stops current playback.
`Stop` clears queued playback and cancels the intent to play after a pending
render; it does not cancel the renderer itself.

Every edit has an application-local generation independent of the saved document
revision. A finished render from an older generation is discarded. An old
waveform may remain visible in gray, with an explicit previous-revision label;
it cannot be played as if it represented the current edit. Editing stops queued
playback. The current prototype waits for an in-flight render when closing.

The output device opens only when playback is requested. A missing device leaves
the editor usable and shows an error. Rendering and headless UI smoke require no
microphone and no output device. There is no microphone recording UI yet.

## Lua operations

Build with `GT_ENABLE_LUA=ON` (the normal desktop preset). Expand `Lua command`,
enter a script path, and select `Run script`; or apply a script at launch:

```sh
python tools/dev.py editor examples/campfire.gtrk.json --script scripts/soften.lua
```

The first Lua API is deliberately coarse:

| Function | Meaning |
| --- | --- |
| `gtracker.transpose(semitones)` | Shift the fret values of pluck events; tuning and damping locations stay fixed. |
| `gtracker.set_tempo(bpm)` | Set pattern tempo. |
| `gtracker.scale_strength(factor)` | Multiply pluck/body-hit strengths; damping amounts stay fixed. |
| `gtracker.add_note(beat, string, fret, strength)` | Append a pluck with a generated unique ID; string is one-based and strength defaults to `0.7`. |

These operations work on a copy. The caller receives a new revision only after
the script finishes and the whole resulting document validates. Lua/native
errors and out-of-range results leave the original document intact. Values are
not silently clipped: for example, a transposition that puts a pluck below fret
zero is rejected. C++ exceptions are contained inside the native Lua callbacks.

Scripts run as **trusted local code**, with Lua's ordinary standard libraries.
They can access files and perform other operations allowed to the application.
The document transaction does not roll back those external side effects. This
is an authoring automation facility, not a sandbox for untrusted song files.
The saved music document contains declarative events, not executable Lua.

## Unattended UI checks

The executable accepts these flags:

```text
gtracker_editor [pattern.gtrk.json] [--script PATH]
                [--smoke-test] [--frames N] [--screenshot PATH.bmp]
```

`--frames N` provides a bounded UI run. `--smoke-test` also starts the real offline
renderer, draws at least the requested number of frames (eight by default), and
waits for a current render before exiting successfully. It reports a failure if
rendering fails or the UI closes before completing. There is a 30-second smoke
deadline; shutdown still waits for any already running native render.

`--screenshot` saves a BMP from the rendered framebuffer immediately before the
last presentation; its parent directory is created as needed. It implies eight
frames when no frame limit is supplied. Use `--smoke-test` with it to include the
completed waveform.

Linux/macOS shell example using SDL's dummy video driver and software renderer:

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
  python tools/dev.py editor examples/campfire.gtrk.json \
  --smoke-test --frames 8 --screenshot .local/editor-smoke.bmp
```

PowerShell example:

```powershell
$env:SDL_VIDEODRIVER = "dummy"
$env:SDL_RENDER_DRIVER = "software"
$env:SDL_AUDIODRIVER = "dummy"
python tools/dev.py editor examples/campfire.gtrk.json --smoke-test --frames 8 --screenshot .local/editor-smoke.bmp
Remove-Item Env:SDL_VIDEODRIVER, Env:SDL_RENDER_DRIVER, Env:SDL_AUDIODRIVER
```

This proves native startup, the actual ImGui/ImPlot drawing path, bounded frame
execution, offline rendering, and BMP capture. It does **not** prove physical
display interaction, keyboard/mouse usability on a particular OS, audio-device
playback, listening quality, or microphone capture. Record those checks
separately in a handoff.

## Current scope

Implemented: pluck/damp/body-hit pattern editing, basic instrument parameters,
JSON load/save, document undo/redo, a small Lua command API, asynchronous rendering,
queued playback, waveform inspection, and unattended UI capture.

Future work includes recording, transcription integration, standard slide/hammer/
pull-off markers and their physical gestures, continuous expression curves,
multiple patterns and arrangement, course-aware twelve-string instruments,
timeline zoom/scroll, richer keyboard tab entry, grouped drag undo, unsaved-work
protection, render cancellation, output-device selection, and reverb controls.
The editor currently uses SDL's maintained 2D renderer backend; migration to
SDL_GPU is optional future work, not required to use or test this prototype.
