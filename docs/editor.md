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
suggests `sketch.gtrk.json` in SDL's per-user gtracker application-data folder.
`--data-dir PATH` overrides that folder. Bundled scripts resolve beside the
executable; `--resource-dir PATH` overrides that resource root. Explicit pattern,
script and override paths resolve against the process working directory.
`tools/dev.py editor` supplies the repository resource root and `.local/` data
folder for development, so generated work stays ignored. The prototype's path fields
hold at most 1,023 UTF-8 bytes. Oversized launch/display paths fail clearly rather
than being silently truncated or applied to an unintended save location.
Pattern, WAV and Lua filenames round-trip through UTF-8 on Windows as well as
Unix; the CLI uses Windows' original wide command-line arguments. The bundled
default UI font has limited glyph coverage, so some non-Latin filename characters
appear as fallback glyphs even though the filename bytes are preserved.

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
- **Keyboard tab entry:** click a string/body cell to activate its outlined
  cursor. Bare arrows move by the current snap and between lanes; Home/End reach
  the first/final snap position. Navigation edits no musical data and preserves
  an existing cursor's fractional offset. Type a fret with the number row or
  numeric keypad: two digits within 0.8 seconds form one fret and one undo step.
  Frets are 0–36; an invalid second digit leaves the first and reports an error.
  A later entry replaces the cell as a separate edit. Digits keep an existing
  event's ID/timing and update linked initial slide contact in the same operation.
  `X` enters full damping on the string; `B` enters a body hit at the cursor beat.
  A selected/unique existing event is replaced, otherwise a new event is added;
  ambiguous coincident events require selection. Escape/Tab leaves tab input.
  Text/numeric fields, popups, dialogs and window focus loss protect musical
  entry; click the canvas again to resume. Retained context outside the pattern
  does not activate entry.
- **Pattern settings:** edit BPM and pattern length above the canvas. Reducing
  the length past existing events is rejected; move or delete those events first.
- **Instrument settings:** expand `Instrument controls` to adjust decay,
  brightness, and body mix. Tuning is currently edited in the JSON document.
- **Undo/redo:** the buttons and Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z, or Ctrl/Cmd+Y restore
  document edits. Up to 128 prior document states are kept in memory. A Lua script
  is one document edit. One slider drag or numeric-field activation is one undo
  operation; a later drag starts a separate operation. A control that returns to
  its starting value leaves no extra undo step.

The path field at the top is explicit: enter a filename, then select `Load` or
`Save`. Ctrl/Cmd+S saves to that path. A `*` by the title marks a changed document.
The marker compares musical content with the last successful save, so undoing
back to that content makes the document clean even though its revision advances.
Loading clears the current undo/redo history.

Loading another pattern or closing the window with unsaved edits opens a
**Save / Discard / Cancel** dialog. Escape cancels. The dialog has a separate
save path, initially the current document's last saved filename: choosing Save
before a pending Load does not overwrite the pattern being loaded. Failed saves
or loads keep the current document and dialog available. Explicit bounded
`--frames` runs still exit at the requested limit for unattended checks.

## Recovery copies

While the app runs, changed unsaved scores get a normal pattern copy every five
seconds under `DATA_DIR/recovery/`. Development uses ignored `.local/`; installed
builds use the configured writable user-data folder. The original score and
source audio are untouched. Each session owns a unique file and updates it
atomically. A failed write retains the previous complete copy and reports an
error. Recent edits since the last successful checkpoint can still be lost.
This is not a guarantee against power loss or storage failure.

At startup, **Review recovery copies** opens the inspector's **Recovery copies**
section. Copies are listed newest first. **Open recovered copy** uses the normal
unsaved-work guard, loads the score for review and suggests a new ordinary save
filename outside the recovery folder. Review and Save that copy before manually
replacing an original. Opening a copy never automatically replaces current edits.
Explicitly opening a recovery filename on the CLI also suggests a new save path.

Copies preserve versioned score data, including IDs, fractional timing, tuning,
instrument model, slides and production. They do not contain undo history,
transport, source PCM, or a take association. The original take bundle/audio/job
files remain intact; reopen those separately for source comparison. Saving or
explicitly discarding a session clears only its own recovery file. Interrupted
sessions' copies remain, and there is no automatic deletion of older sessions.

The list scans at most 512 directory entries and opens up to 20 recent valid
copies. Unreadable/future-format files stay on disk and are reported. Use
**Refresh recovery copies** to rescan; manage older files only after checking the
saved work you want to keep. An unwritable data directory leaves editing/manual
Save available and reports the recovery failure.

`--no-recovery` disables recovery writing and discovery for that session, in both
the direct editor and `python tools/dev.py editor`. There is no background job
after the editor exits. Generated dummy-driver tests forcibly terminate an owned
editor after a checkpoint, validate/reopen its score and verify unchanged
original bytes; that is separate from physical-window or hard-power-loss testing.

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
playback. **Cancel render** stops an in-flight preview while retaining the last
completed waveform/audio. Progress shows audio generation, then waveform
preparation. Editing automatically cancels obsolete work. Closing requests
cancellation and joins the worker; it never leaves an editor-owned worker behind.
Cancellation is checked at block/metrics/preview boundaries and does not change
the pattern, undo history or source files.

The output device opens only when playback is requested. A missing device leaves
the editor usable and shows an error. Rendering and headless UI smoke require no
microphone and no output device. There is no microphone recording UI yet.

Use `python tools/dev.py editor --take .local/takes/NAME/take.json` to open a
[preserved take](takes.md) and its editable pattern. The inspector also has a
**Source take** path and **Open take** button. **Play source** compares original
PCM with **Play synth**; the waveforms share a beat axis, including retained
source context. **From beat 0** skips selected pre-roll during preview only.
Saving an attached pattern preserves the take association; **Detach** permits a
separate pattern copy. Source bytes and analysis proposals are never rewritten
by preview playback. The metadata-only worker supplies no musical proposals.

The separate **Note proposals** inspector reviews optional
[single-note research jobs](proposals.md). Load a sidecar for the attached take,
expand notes and explicitly choose each string/fret alternative. All notes start
skipped. **Accept selected plucks** adds one undoable batch with fractional source
timing and your chosen strength. A changed score/source rejects stale acceptance;
the old metadata result and original PCM remain unchanged. This experiment does
not recognize chords, articulations, or actual played strings.

The tab, source and synth share the same beat viewport and screen-pixel
positions. **Pattern** fits the editable region; **Context** also shows retained
pre-roll and rendered/source tails, with outside-pattern tab space dimmed. Wheel
zooms around the pointer and middle-drag pans; Shift+wheel also pans the tab.
The waveform accepts wheel/middle-drag navigation. The `-`/`+` controls zoom
around the view center. Context clicks cannot insert events outside the pattern.
The inspector uses the full height beside the tab/wave column, including at the
compact window size.

**Auto scale** fits the waveform's vertical display range. It changes no PCM,
playback gain, timing, document revision or undo history. An old render remains
gray. Source views initially include their retained context; ordinary patterns
initially fit their musical region.

## Production

Expand **Production / room** for saved wet/dry, decay/damping, pre-delay, stereo
width and gain controls. The first edit explicitly stores pattern v2; undo can
restore the original v1 document. Disable **Room enabled** for exact dry mono
bypass. Render again after editing; enabled production renders and plays stereo.
See [production settings and signal checks](production.md). Original source-take
playback remains unchanged.

## Lua operations

Build with `GT_ENABLE_LUA=ON` (the normal desktop preset). Expand `Lua command`,
enter a script path, and select `Run script`; or apply a script at launch:

```sh
python tools/dev.py editor examples/campfire.gtrk.json --script scripts/soften.lua
```

The first Lua API is deliberately coarse:

| Function | Meaning |
| --- | --- |
| `gtracker.transpose(semitones)` | Shift pluck and slide-point frets together; IDs, timing, tuning and damping locations stay fixed. |
| `gtracker.set_tempo(bpm)` | Set pattern tempo. |
| `gtracker.scale_strength(factor)` | Multiply pluck/body-hit strengths; damping amounts stay fixed. |
| `gtracker.add_note(beat, string, fret, strength)` | Append a pluck with a generated unique ID; string is one-based and strength defaults to `0.7`. |
| `gtracker.set_model(model)` | Explicitly select v2 `baseline_0_1`, experimental `contact_0_2` or `retained_0_5`; existing slides prevent switching to baseline. |
| `gtracker.add_slide(beat, string, points)` | Append a v2 contact curve; points contain local `beat`/`fret` values, starting at offset zero. |

See [continuous slides](gestures.md) for the tab controls, model opt-in, shared
initial fret editing, v1 compatibility and an original audible study.

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
                [--take PATH] (instead of a pattern path)
                [--data-dir PATH] [--resource-dir PATH]
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

`python tools/dev.py test --profile desktop` also runs `editor_ui` using SDL's
dummy video/audio drivers. It injects synthetic mouse/key input into native
ImGui frames to check drag grouping, undo/redo, the unsaved modal and shortcut
focus, and exercises stale-render rejection and queued dummy playback. These
are automated regression checks, not physical device or listening evidence.
The headless `editor_document` test covers guarded save/load/close, failed writes,
savepoints, revision bounds and history without SDL.

## Current scope

Implemented: pluck/damp/body-hit and continuous-slide editing, explicit baseline/
contact/retained model selection, reviewed single-note proposal acceptance, basic instrument parameters,
JSON load/save, document undo/redo, a small Lua command API, asynchronous rendering,
queued playback, waveform inspection, and unattended UI capture.

Future work includes editor recording controls (CLI capture exists),
general guitar transcription, hammer/pull-off and additional contact/expression gestures,
an arrangement UI (CLI songs exist), course-aware twelve-string instruments,
chord/selection shortcuts, full session recovery and output-device selection.
The editor currently uses SDL's maintained 2D renderer backend; migration to
SDL_GPU is optional future work, not required to use or test this prototype.
