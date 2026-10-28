# SID tracker implementation plan

- Date: 2026-10-29
- Owner: Timo Saarinen
- Repository: timosaarinen/gtracker
- Baseline inspected: 4d5248f15bc8c670275efaacc4ceb355ae2b085a
- Status: Implementation contract for the first complete SID mode

## 1. Implement this whole plan

Build a usable native SID tracker inside gtracker. Timo should be able to launch it, enter an original tune with a familiar computer-keyboard tracker workflow, arrange heavy and mellow sections, hear the result, save it, and export working C64 music without leaving the application.

This document is intended to be handed to Codex with: "Read docs/sid-tracker-implementation-plan.md and implement it fully."

Read AGENTS.md, README.md, docs/handoff.md, and the relevant current code first. Follow the repository's existing development and publishing workflow. Inspect the current checkout and preserve other people's work. The baseline above is context, not an instruction to reset the repository. Resolve routine implementation choices yourself. Carry milestones M0 through M7 to completion; an exporter-only prototype, a mock UI, or instructions to compose in an external application do not complete this task.

The working SID mode is the priority. Produce the first audible end-to-end slice quickly, then finish the native workflow, original demo, packaging and verification. The user originally needed a song for 29 October; if implementation happens later, retain the same bias toward a small complete tool rather than expanding scope.

## 2. User intent and product decisions

Timo has used Scream Tracker II and other trackers, and Renoise more recently. He wants simple, familiar tracker operation, not a lesson in SID register programming.

The musical reference is the energy and contrast of Rob Hubbard's Commando main and high-score music. The composition must be original. Heavy and mellow passages belong to ONE song and ONE subtune. Use original hard-coded instrument programs. The reference defines the desired sound and arrangement contrast.

The following decisions are settled for this implementation:

| Area | Decision |
| --- | --- |
| Application | A dedicated native SID mode alongside the existing guitar mode |
| Hardware target | One PAL C64 SID, 6581 as the initial sound target, one player call per video frame |
| Voices | Exactly three physical monophonic voices |
| Grid | Vertical tracker; NOTE and INSTR columns for each voice |
| Arrangement | One visible order of synchronized three-voice patterns, grouped into named sections |
| Instruments | A small fixed, versioned bank; choose a preset and enter notes |
| Timing | Four rows per beat; one global integer speed from 4 to 12 ticks per row, default 6 |
| Patterns | 64 rows per pattern in version 1; beat shading every 4 rows and bar shading every 16 |
| Musical events | NOTE with explicit preset, HOLD, OFF |
| Sound generation | Proven GoatTracker player/packer backend initially |
| Editable source | A dedicated versioned .sidtrk.json document |
| Output | Genuine .sid, standalone runnable .prg, and 16-bit PCM .wav |
| Primary delivery platform | Windows 11 desktop; keep portable C++/SDL architecture and report other platform evidence separately |
| Song ending | Finite arrangement followed by a defined release tail and silence |
| Pattern audition | Native loop of the selected pattern, independent of the full-song ending |

Speed is one simple global control. Show the effective approximate BPM beside it. Do not add separate per-section tempos or expose a hexadecimal tempo command. The mellow passage can use half-time phrasing, longer notes and fewer events at the same speed.

The core scope does not include an instrument editor, effect-command grid, sample import, MIDI input, recording, guitar-to-SID conversion, automatic voice allocation, arbitrary polyphony, filter routing UI, multiple SID chips, NTSC support, or a custom 6510 music driver. These can be later work. Their absence must not prevent the complete workflow specified here.

## 3. Existing project boundaries and reuse

The inspected repository is a working acoustic-guitar tracker with C++17 code, SDL3, Dear ImGui/ImPlot, Lua, native CLI commands, versioned score files and a package workflow. Its guitar score and renderer are concrete types, not a generic synthesizer interface.

Useful current files:

| Existing location | Use in this work |
| --- | --- |
| include/gtracker/project.hpp | Understand the existing guitar model; keep the new SID model separate |
| include/gtracker/song.hpp and src/song.cpp | Reuse arrangement concepts, not guitar-specific structures or event expansion |
| include/gtracker/editor_document.hpp | Reuse the document-generation, undo and dirty-state approach |
| src/editor.cpp | Reuse application shell, device selection and native UI conventions |
| include/gtracker/audio_source.hpp and src/audio_source.cpp | Load rendered integer PCM WAV into PcmAudio |
| src/editor.cpp queue_audio and play_source | Existing SDL float-PCM queueing and sample-slice playback plumbing |
| tools/dev.py, tools/package.py and CMakeLists.txt | Extend the established build, CLI and packaging entry points |
| docs/workflow.md and tools/repo_guard.py | Follow current verification and publication rules |

Keep the guitar C ABI, DSP and file semantics intact. Add focused SID modules. Extract a small shared utility where it is useful; avoid making a general audio-engine or document-framework rewrite a prerequisite.

Proposed source organization, adjustable to fit the actual code without changing behavior:

- include/gtracker/sid_song.hpp and src/sid/song.cpp
- src/sid/presets.cpp for original fixed bank data and version
- src/sid/goattracker_export.cpp for GTS5 serialization
- src/sid/tools.cpp for backend execution and diagnostics
- src/sid/preview.cpp for compiled-SID rendering and caches
- src/sid/transport.cpp for prepared PCM playback and cursor position
- src/sid/document.cpp and src/sid/editor.cpp for the dedicated mode
- schema/sid-song.schema.json
- examples/sid/heavy-mellow.sidtrk.json
- docs/sid-tracker.md for the finished user guide
- focused tests under tests/ using the repository's conventions

These modules should form an independently testable gtracker_sid component. The native score/compiler must not depend on the GUI. The SID audio callback must only consume prepared PCM.

## 4. The native screen and first-use experience

Provide New SID Song and Open SID Song in the normal application flow. Opening .sidtrk.json should select the SID mode automatically. Keep opening existing guitar files working.

Use one main screen:

1. Top toolbar: Play Song, Loop Pattern, Pause/Resume, Stop, Edit indicator, selected octave, edit step, global Speed with approximate BPM, Save, Export.
2. Left panel: song order grouped under section names.
3. Center: three-voice vertical tracker grid.
4. Right panel: fixed instrument palette with number, name and one-line description.
5. Bottom: short contextual keyboard help, job status and actionable errors.

Suggested initial voice labels are Lead, Arp / Counterline, and Bass / Drums. These labels are guidance only; any preset can be used on any voice. The same voice can alternate between bass and percussion on different rows.

A new song starts in visibly enabled Edit mode, with the note grid focused, a selected lead preset, octave 4, edit step 1, speed 6, and two empty patterns under HEAVY and MELLOW sections. It is immediately possible to press a piano key and enter a note. No sound-design setup is required.

Display note names such as C-4 and F#4. Use one documented octave convention: scientific middle C is C4, MIDI note 60. Keep the display, internal pitch, audition and exported frequency consistent. Show row numbers and instrument IDs in decimal; hexadecimal editing is unnecessary in this mode.

All normal controls must fit at the existing application's reasonable minimum size, with scrolling where needed, readable text, and functional Windows scaling. Preserve keyboard focus during note entry. A small Follow control can keep the playback row visible; it must not prevent intentional navigation.

The primary export button is Export SID. Its adjacent menu offers Export WAV and Export C64 Program. Normal users should not need to know the names or paths of backend executables.

## 5. Keyboard and editing contract

Use these explicit application bindings. Familiar Renoise piano and edit conventions are useful; the function-key aliases below are this application's choices.

| Action | Binding |
| --- | --- |
| Play or pause/resume current transport | Space |
| Play the whole song from the beginning | F5 |
| Loop selected pattern in isolation | F6 |
| Play song from selected order occurrence and row | F7 |
| Stop transport, silence audition and cancel pending autoplay | F8 |
| Toggle Edit mode when no dialog owns Escape | Esc |
| Lower piano octave | Z S X D C V G B H N J M |
| Upper piano octave | Q 2 W 3 E R 5 T 6 Y 7 U |
| Enter OFF | A; Caps Lock may be an additional alias |
| Clear selected event or block without shifting time | Delete |
| Move through rows and columns | Arrow keys |
| Move to next or previous voice | Tab and Shift+Tab |
| First or last row | Home and End |
| Move by 16 rows | Page Up and Page Down |
| Select block | Shift+Arrows or mouse drag |
| Cut, copy, paste | Ctrl/Cmd+X, C, V |
| Undo, redo | Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z; Ctrl+Y alias |
| Octave down/up | Numpad / and * plus visible controls |
| Previous/next preset | Numpad - and + plus palette click |
| Edit step | Visible 0, 1, 2, 4 selector; Ctrl+1/2/4 aliases |
| Save, Save As, Open | Ctrl/Cmd+S, Shift+Ctrl/Cmd+S, Ctrl/Cmd+O |

Required behavior:

- A piano key in Edit mode writes BOTH note and selected instrument, auditions it, and advances by the edit step.
- With Edit off, it auditions without changing the document or moving the cursor.
- A new NOTE retriggers its preset. A blank row is HOLD and leaves the running voice alone. OFF releases it.
- Instrument IDs are explicit on NOTE events. HOLD and OFF carry no instrument. Selecting a preset on an empty row changes the entry preset; it does not create an invisible instrument-only event.
- Editing the INSTR field on an existing note changes that note's preset. Clearing a note clears the whole note event. A palette click only selects the entry preset; an explicit Apply to Selection action changes existing selected notes as one undo operation.
- Route grid keys by subcolumn: piano keys enter notes when NOTE has focus; digits edit the preset ID when INSTR has focus. The upper-octave sharp keys must not conflict with numeric instrument entry.
- Suppress OS key repeat for note entry. Normal navigation can repeat.
- Text fields, file dialogs and menus own their keys. Typing a filename must never enter notes.
- Escape closes the current dialog first and must not toggle Edit in the same event.
- Use a consistent physical piano-key layout on Finnish QWERTY. Give visible alternatives to numpad-only shortcuts.
- Paste is rectangular and bounded to the current pattern. Preserve HOLD/OFF and presets. Do not silently wrap excess rows into another pattern.
- Transpose selection by semitone or octave through a small menu or buttons; transpose pitched notes only. Fixed-pitch drum events retain their sound. Reject a change that would exceed supported pitches instead of silently clamping.
- Note entry is one undo action per press. Block edits, paste, transpose, duplicate and order operations are grouped into meaningful undo steps.
- Space from stopped starts the song at the selected order occurrence and row. Space while playing pauses; Space while paused resumes the same transport. F5 always starts the song at its beginning. F8 stops while retaining the edit selection. For a pattern not placed in the song, use F6 or place it before song playback.
- For version 1, a mutating edit during transport stops playback and invalidates its prepared render. The change is audible on the next Play. Do not build live recording or seamless hot editing before completing this plan.

Do not require users to type raw wavetable, ADSR, pulse or effect bytes.

## 6. Patterns, sections and editable song format

### Arrangement behavior

One pattern contains all three voices and exactly 64 rows. One order entry references one pattern. All three voices advance together.

Provide these operations:

- New Pattern: create an empty pattern and place it after the selected order entry.
- Repeat: add another reference to the same pattern.
- Duplicate: copy the pattern under a new ID and add an independent variation after the selected entry.
- Move Up / Down for an order entry, plus Move to Section for moving it to another named section.
- Remove from Song: remove that order entry while retaining the pattern definition.
- Add / Rename / Remove Section; removing a section with entries must offer an explicit undoable choice to move or remove those entries.
- Rename patterns.
- Delete an unused pattern through a clear command.

Show "Used N times" for a pattern with multiple references. Editing a shared pattern affects every reference. Duplicate is the visible way to make a variation.

HEAVY, MELLOW, INTRO and RETURN are ordinary editable section names. A section name never changes instruments, tempo, gain or hidden playback state. A transition is authored through notes, holds, releases and preset changes.

### Source model

Use a self-contained UTF-8 .sidtrk.json file. Commit its JSON schema and validate it in the native code. A representative shape is:

    {
      "format": "gtracker.sid",
      "schema_version": 1,
      "name": "Heavy and Mellow",
      "author": "Timo Saarinen",
      "revision": 1,
      "instrument_bank_version": 1,
      "target": { "clock": "pal", "sid_model": "6581" },
      "timing": { "ticks_per_row": 6, "rows_per_beat": 4 },
      "patterns": [
        {
          "id": "p01",
          "name": "Heavy A",
          "rows": 64,
          "voices": [
            [{ "row": 0, "type": "note", "note": 60, "instrument": 1 }],
            [],
            [{ "row": 0, "type": "note", "note": 36, "instrument": 2 }]
          ]
        }
      ],
      "sections": [
        {
          "id": "section01",
          "name": "HEAVY",
          "order": [{ "id": "order01", "pattern_id": "p01" }]
        }
      ]
    }

This is an illustrative schema fixture, not the musical demo. Sparse absent events mean HOLD; explicit OFF records use row and type only. Do not serialize redundant UI state into every event. Keep stable IDs for patterns, sections and order entries so references and diagnostics survive editing.

Initial bounds:

- Exactly three voices and 64 rows per pattern.
- Up to 64 source patterns, 128 total order entries and 16 sections.
- Source file at most 1 MiB.
- Row indices 0 through 63; at most one event per row per voice.
- Pitched note range MIDI 24 through 95, displayed as C1 through B6.
- Fixed arpeggio offsets must also fit the backend's actual frequency-table range; validate the expanded pitches.
- Integer speed 4 through 12; four rows per beat.
- Metadata lengths and encoding must be validated for both the editable file and PSID header.

Longer songs are a later expansion after measuring the initial native preview memory budget. At the slowest supported speed, 128 patterns occupy approximately 32.7 minutes; use bounded rendering/streaming or reject a preview that exceeds its declared memory limit before allocating it. The source and exporter must never silently shorten the arrangement.

For blank documents, skip empty sections in playback; a completely empty arrangement or one containing no NOTE events may be saved but cannot export as a completed tune. Provide a specific message directing the user to add a pattern.

Preserve Unicode authoring names. For PSID's limited text fields, make a deterministic documented encoding/truncation policy and report any export-only shortening. Never damage the source metadata.

Save/load, undo/redo and recovery must preserve all musical data and bank version. Unknown schema or bank versions receive a clear error. Saving does not execute Lua or any embedded code.

## 7. Fixed original instrument bank

Implement bank version 1 with stable numeric IDs. Tables and parameters are source-controlled original data. Once this bank is released, changing its sound requires a new bank version; loading an old project must not silently change its presets.

Use this bank:

| ID | Name | Role and behavior |
| --- | --- | --- |
| 01 | Heavy Lead | Bright pulse, animated width, clear attack, delayed moderate vibrato |
| 02 | Pulse Bass | Solid pulse bass with a short punchy attack and controlled sustain |
| 03 | Bright Major Arp | Bright root-relative 0, 4, 7 semitone cycle |
| 04 | Bright Minor Arp | Bright root-relative 0, 3, 7 semitone cycle |
| 05 | Warm Lead | Softer pitched lead, gentle movement and slower articulation |
| 06 | Soft Bass | Round triangle-oriented bass with a smooth onset |
| 07 | Soft Major Arp | Softer root-relative 0, 4, 7 cycle with a distinct texture |
| 08 | Soft Minor Arp | Softer root-relative 0, 3, 7 cycle |
| 09 | Kick | Fixed absolute-pitch transient and downward body sweep |
| 10 | Snare | Fixed noise-led transient with a short body |
| 11 | Hat | Fixed short noise articulation |

Make the bank audibly useful, not eleven copies with different labels. Heavy should get energy from attack, rhythm, pulse movement and density. Mellow should have more space and a contrasting envelope/timbre. Do not use clipping as a substitute for heavy sound.

For the first bank:

- Disable the global SID filter and use a fixed master volume.
- Avoid ring modulation, oscillator sync and multi-voice patches.
- Every preset works independently on any physical voice.
- Melodic tables follow the entered note; arpeggio preset names state the fixed chord quality.
- Drum pitch is fixed by the preset; ordinary keyboard note entry triggers it. Store/display a canonical drum note or clearly display the fixed pitch behavior.
- Every preset initializes or stops the pulse/wave/vibrato state it relies on. A warm lead following a drum or bass must not inherit an unintended program.
- Drum programs end predictably. A note need not require a manual OFF immediately after every drum.
- All melodic releases must settle within the declared three-second final render tail.
- Use the proven player's gate/hard-restart mechanism. Verify attack behavior at speeds 4, 6 and 12 and at heavy-to-mellow transitions. The conventional two-tick pre-note restart is a starting point to test, not a claim about every physical SID revision.

The user chooses a preset, not its synthesis parameters. Developers may tune bank version 1 during implementation, using repeatable fixtures and listening. Record the final bank content hash in preview/export provenance.

## 8. Compiler and C64 output

### Pipeline

Use:

    SID document -> validated SID score -> GTS5 .sng
                 -> pinned gt2reloc -> .sid
                 -> exact exported bytes -> sidplayfp -> PCM preview/WAV

Keep three-voice pattern identity through compilation. Emit the corresponding single-voice GoatTracker patterns and order lists, reusing compatible data. Do not flatten the arrangement into repeated note copies before the packer sees it.

GoatTracker's editable .sng contains four-byte rows; its packer performs runtime compression. Do not reimplement that packer in this milestone. Source references R1 through R5 explain the format and checked implementation.

Use GoatTracker 2.77 as the inspected reference. Establish and record the exact source revision/archive checksum used by the build. A compatible later bug-fix release is acceptable if required by the target toolchain, with the choice and behavior verified and pinned.

The documented starting invocation is:

    gt2reloc song.sng song.sid -P -S1 -B1

These options select PAL, one call per frame and buffered writes. Verify against the pinned executable. Do not invent a SID-model flag: the inspected command-line packer defaults to 6581, and some similarly named options control unrelated features.

### Serializer and compiler requirements

- Use the upstream manual AND saver as authority for GTS5 lengths, terminators and restart bytes.
- Emit NOTE, HOLD and OFF with their distinct meanings.
- Set tempo explicitly before the first musical event; the global speed control must map correctly to the player's counter convention.
- Define initialization/pre-roll so the first note is not lost, delayed by an accidental whole row, or retriggered incorrectly.
- Verify the scientific-pitch to GoatTracker-note mapping with known pitches, including A4 at approximately 440 Hz; do not guess an octave offset from labels.
- Map a synchronized global pattern into three backend patterns. Deduplicate equal content without altering authoring IDs.
- Validate actual backend pattern count, order lengths, table sizes, packed-pattern sizes and address ranges. The inspected GoatTracker pool has 208 patterns; that limit applies after conversion, including generated terminal/initialization patterns.
- Map diagnostics back to source section/order, pattern, row and voice.
- Reject unsupported or overflowing content explicitly. Do not drop a voice, truncate a pattern, alter pitch, or approximate a guitar score.
- All final file writes are atomic. Failed or cancelled exports preserve prior outputs.
- Backend invocation uses argument arrays, owned processes and explicit working directories. Handle paths with spaces and Unicode; do not build shell command strings from project names.

Compression should exploit pattern reuse, repeated state, table reuse and the proven packer's unused-feature removal. Record actual sizes. No custom entropy codec, Exomizer layer or custom player is required for completion.

### End of song

Version 1 songs have a finite order. At the end, compile a terminal sequence that releases all three voices, allows their envelopes to finish, then repeats silent hold data. This uses the player's normal sequence machinery while giving a finite musical result. Terminal data and its additional order entries must be included in capacity checks.

The .sid and standalone .prg must both follow that ending. WAV/full-song preview renders the authored duration plus exactly three seconds of release tail. Verify released gates and a documented measured quietness threshold at the tail, rather than demanding bitwise-zero PCM. Report final DC level and DC-removed RMS; residual emulator noise must not be mistaken for a stuck musical voice. Report the nominal musical duration and rendered duration separately.

Pattern-loop audition uses a separate temporary looping preview arrangement; it does not change the saved song ending.

### PSID

The initial output is single-SID, one-subtune PSID v2NG with explicit PAL/6581 flags, correct initialization/play addresses and normal VBI timing. Validate the generated header and payload against the current HVSC specification, reference R6.

Do not write the PRG load-address prefix twice. Check that load/init/play addresses are within the produced program and that code/data fit the declared memory map.

### Standalone runnable PRG

Export C64 Program must create an actually runnable program, not merely the music-only .prg emitted by the packer.

Provide a small reviewed 6502/6510 launcher with:

- A valid BASIC SYS entry.
- A documented memory layout and overlap checks.
- Player initialization with A = 0.
- One play call per PAL raster frame.
- Correct interrupt ownership, acknowledgement, CPU register preservation and memory mapping.
- Predictable initialization and song ending.

Keep the same compiled music payload in the SID and PRG paths; do not independently change instruments or tempo. If a second packer output is needed, verify its music payload matches the SID export. Use a pinned assembler or a simple reviewed fixed launcher with bounded patch fields; do not build a new assembler.

Prove LOAD/RUN playback in VICE. A valid file header alone does not establish this feature.

## 9. Timing, preview, audition and transport

### Timing

Use C64 player ticks, not desktop elapsed milliseconds, as musical time. For the common PAL constants, 985248 clock cycles/second and 19656 cycles/frame give approximately 50.12454 frames/second.

With four rows per beat:

    effective BPM = 60 * PAL player calls per second / (4 * ticks per row)

Default speed 6 is approximately 125.31 BPM. Display an approximate BPM derived from the actual target. Heavy and mellow sections share this clock.

The compiler produces a row/order timeline in player ticks. The renderer records the fixed relation between initialization and the first musical tick. Use those facts for PCM positions and playhead tracking; do not assume sample zero is row zero. Allow for the driver's documented note-start/hard-restart behavior.

### Authoritative song playback

Render the exact .sid bytes produced for export from initialization through the full arrangement and final tail. The native application plays that immutable PCM using the existing SDL path.

Required transport:

- Play song from the start.
- Pause/resume.
- Stop immediately, clearing queued audio and audition.
- Play from a selected order occurrence and row using the corresponding point in the full-song PCM.
- Correct current section/order/pattern/row indication.
- Stable timing across repeated references and heavy/mellow boundaries.

Seeking into the full-song PCM preserves the SID history already rendered before that point. Do not cold-start a selected occurrence and claim it is equivalent.

The existing PCM loader accepts integer WAV. Request 48 kHz, 16-bit PCM explicitly; mono is appropriate for a single SID. A finite duration must be supplied to the renderer. Respect native file/sample bounds and use bounded streaming or a clear preflight error for long renders.

Native playhead timing must be based on samples actually consumed/queued with documented output-latency accounting, rather than a separate wall-clock animation. The current queue_audio plumbing needs transport work; do not assume pause, seek or position tracking already exist.

### Pattern loop

F6 plays the selected pattern in isolation. It starts from reset state; a leading HOLD is silent until that voice receives a NOTE. Explain this briefly in the user guide.

Generate a temporary looping SID arrangement through the same compiler. An initial implementation may prepare several iterations: play the cold first iteration once, then repeat a validated warmed complete PCM loop. A leading HOLD is silent on the first pass and may carry the preceding iteration's voice state on subsequent passes. Validate that the chosen loop has no distracting click or truncation. This is an isolated editing audition; full-song playback/export remains the continuous authoritative render with its real preceding context.

If the PCM loop cannot make a given pattern usable, improve the rendered repeat segment or the emulator transport. Do not mark the loop feature complete while it audibly glitches.

### Immediate note audition

A fixed instrument bank makes cached note audition practical:

- Generate short isolated-note fixtures through the same player and SID renderer.
- Each has initialization/pre-roll, the exact preset and pitch, a fixed audition gate, OFF, and a release tail.
- Cache actual pitches; do not pitch-shift a sampled note or add a second approximate synthesizer.
- Prepare an audition atlas or batched caches during development/package construction. Avoid one process launch per keypress.
- Cover the supported melodic range and the three fixed drum sounds in the finished package, or generate the required cache offline before making that range available for immediate audition.
- A new one-shot audition may replace the preceding one. Held-key synthesis and polyphonic desktop audition are outside version 1.
- A cache miss runs asynchronously and cannot block note entry.
- Warm-cache note entry should feel immediate; aim for under 50 ms input-to-queued-audio on the primary development machine and measure the actual result.
- Explain in developer documentation that isolated note audition is a patch preview, while the whole-song render proves rapid retrigger/context behavior.

### Determinism and workers

Use a pinned sidplayfp/reSIDfp configuration, including fixed power-on delay, chip/clock, filter/resampling settings, output format and duration. Do not depend on a user's sidplayfp.ini, author-specific filter defaults or HVSC song-length database.

A documented starting command, to validate against the pinned binary, is:

    sidplayfp --residfp -vpf -mof --delay=0 -f48000 -p16 -os1 -t90 -w song.sid

The implementation derives duration from the song instead of hard-coding 90 seconds.

Cache identity includes canonical score, bank version/hash, compiler and packer revision/options, exact SID hash, renderer version/configuration, sample format and render range/duration. Require repeatability for the same pinned toolchain; do not invent a cross-architecture PCM identity guarantee.

Compile/render off the UI and audio threads. Use immutable document snapshots and generation IDs. Reject stale results after edit/load/undo; an older result must not autoplay or replace the current preview. Stop and cancellation own and terminate only this job's processes. Errors leave the editor usable and the source score intact.

## 10. CLI and developer entry points

Add unattended native commands consistent with the existing CLI style:

    gtracker_cli sid-validate FILE --json
    gtracker_cli sid-export FILE --output song.sid --json
    gtracker_cli sid-render FILE --output song.wav --json
    gtracker_cli sid-export-prg FILE --output song.prg --json
    gtracker_cli sid-export-sng FILE --output song.sng --json
    gtracker_cli sid-info FILE --json

Expose practical wrappers in tools/dev.py, including launching the SID editor and preparing/checking backend tools. Exact argument spelling can follow the existing parser, but the final docs must show commands that really work.

The JSON report should contain source revision/hash, bank version/hash, backend versions, target, pattern/order counts, authored and rendered duration, output paths/hashes/sizes, load/init/play addresses where relevant, and structured errors with musical locations. Report measured code/data/workspace only if the tool provides or the implementation reliably derives them; do not estimate CPU raster cost as if measured.

Opening an exported .sng in GoatTracker is useful during validation. It is not the primary user workflow and importing arbitrary edited .sng files is not part of version 1. The .sidtrk.json remains authoritative. Never overwrite a user-edited external file implicitly.

## 11. Original heavy and mellow demonstration song

Ship a musically coherent original example that exercises the product. Working title: Heavy and Mellow. Timo can rename it and replace or edit its material. Keep example provenance explicit in the documentation: generated original demonstration material is not presented as a personally performed or authored Timo recording.

Aim for approximately two minutes at default speed, using about sixteen four-bar pattern placements:

- INTRO: 2 placements.
- HEAVY A: 4 placements.
- MELLOW B: 4 placements.
- HEAVY RETURN: 4 placements.
- OUTRO: 2 placements followed by release.

This is a composition brief, not a demand to pad weak material to an exact duration. Use reusable motifs and a small number of editable pattern definitions.

The example must demonstrate:

- A recognizable original lead motif.
- Punchy bass/drum sharing in the heavy passage.
- Major/minor arpeggio presets used deliberately.
- A clearly contrasting mellow passage with lower density, warmer presets and longer notes.
- A return that develops the earlier material.
- An intentional transition into the mellow passage and out of it.
- A finite musical ending without stuck notes.
- More than one use of a shared pattern and at least one independent variation.

Listen to both sections and the transitions. Document actual listening observations separately from signal metrics. Avoid extreme gain or PC-only reverb, compression or distortion: the saved SID program must produce the musical character itself.

Commit original textual scores and preset definitions. Generate WAV/SID/PRG files and audition caches under ignored .local/ during development and include them in the assembled developer bundle as appropriate.

## 12. Packaging and platform support

The primary finished package is a relocatable Windows 11 desktop bundle. It must complete this workflow offline without a compiler, Python package download, system PATH installation of SID tools or external composition application.

Package:

- Native application and required runtime dependencies.
- Pinned packer and renderer executables for the target platform.
- Backend configuration, version/checksum manifest and applicable notices.
- Original fixed bank and example score.
- Prepared audition data or a validated offline preparation path that preserves immediate note-entry behavior.
- Short user guide and exact export instructions.

Use resources beside the executable and a writable per-user data/cache directory. Moving the package or running it from a read-only installation location must work.

Third-party binaries and generated audio belong in ignored build/package output, not Git. Commit reproducible source/download/build recipes and checksums. Resolve dependencies during development/build, never by an unexpected download from the first-use note-entry flow.

GoatTracker's editor/packer license and its explicit player-source/song-binary exception are different. Preserve the relevant notices and follow the distribution requirements of every included executable. Keep the chosen packer external initially; no GPL library-linking decision is required for this architecture. Do not change gtracker's own distribution license as an incidental step.

Verify the selected simple exported driver renders in a clean ROM-free sidplayfp environment. Do not ship proprietary ROM dumps or leave a hidden ROM-file requirement in the finished package. If the selected backend unexpectedly depends on ROMs, resolve that backend/driver choice before claiming offline completion.

Maintain portable C++/SDL code. Build/test on other available hosts where practical and report evidence precisely. An unavailable Intel Mac or physical Linux desktop does not justify leaving the Windows feature incomplete, and a Windows pass does not qualify those hosts.

## 13. Implementation milestones

### M0 Establish the executable path

Read the repository instructions, inspect local work, and run the prescribed doctor/CI-status commands. Set up pinned backend tools in the normal ignored dependency area. Generate one tiny original .sng fixture, export it, validate its header and render audible PCM.

Aim to settle this path within the first two focused hours. If it fails, diagnose the specific dependency or format issue and continue toward the same native product. An external tracker is a diagnostic tool, not the final acceptance target.

Deliverable: real original fixture -> genuine SID -> bounded integer PCM, with exact commands and versions.

### M1 Implement SID document, presets and compiler

Add the versioned model, validation, fixed bank, serializer, semantic mapping, finite ending, size/range checks and CLI export. Preserve source pattern references and meaningful diagnostics.

Deliverable: all presets and note/hold/off semantics export deterministically, and the editable file round-trips.

### M2 Implement playback and auditions

Add owned backend jobs, exact-SID full-song rendering, timeline mapping, native PCM transport, warmed pattern audition, and the prepared note-audition bank/cache. Handle stale jobs, cancellation and missing tools.

Deliverable: native audible note feedback and song/pattern playback with working stop/pause/current-occurrence behavior.

### M3 Implement the tracker screen

Add file-mode selection, three-voice grid, defined keyboard behavior, preset palette, meaningful block edits and undo. Ensure focus behavior is correct.

Deliverable: a user can create and hear a short phrase entirely in gtracker.

### M4 Implement arrangement and persistence

Add section/order editing, repeat versus duplicate semantics, used-count feedback, complete save/reopen, dirty-state protection and recovery.

Deliverable: a user can make heavy/mellow/return sections, save the song, reopen it and continue editing without losing intent.

### M5 Finish outputs and the musical example

Add the standalone PRG launcher, WAV/SID export UI, metadata policy, original heavy/mellow example and final preset tuning. Test transitions and song ending.

Deliverable: the same original composition is editable in gtracker and audible in each exported format.

### M6 Package and verify the user journey

Extend developer packaging, prepare offline resources, test a moved package without global backend tools, and exercise real native interaction on the primary host. Fix problems exposed by the workflow.

Deliverable: usable relocatable desktop package and the completed focused acceptance evidence below.

### M7 Document, commit and hand off

Update README, the finished SID user guide, architecture/format notes, third-party notices and docs/handoff.md. Preserve the implementation contract; record deviations and their concrete reasons rather than quietly weakening requirements.

Run appropriate final checks, inspect staged changes, run the repository guard, fetch/review remote changes, and publish small finished commits according to AGENTS.md. The final report names the revision, package location, commands, actual tests, actual GUI/listening evidence and any precisely scoped unresolved environmental limit.

Do not stop at M0/M1 or return a second plan as the implementation result.

## 14. Focused verification and completion gates

Tests must cover distinct failure risks. Prefer a compact set of meaningful fixtures over an exhaustive cross-product or tests that merely mirror implementation.

| Gate | Required evidence |
| --- | --- |
| Editable source | Save/reopen preserves pattern/order identity, sections, notes, presets, timing and bank version |
| Editing | Enter, clear, OFF, select, paste, transpose, repeat, duplicate and undo behave as specified |
| Pitch | Known notes have the intended octave/frequency in the real export; fixed drums remain fixed |
| Presets | All eleven presets work on all three voices and do not inherit unwanted prior state |
| Event semantics | HOLD across pattern boundaries, OFF and rapid same-note retriggers are correct |
| Compiler | Correct lengths/terminators, tempo initialization, finite ending and capacity diagnostics |
| Determinism | Identical score/bank/pinned tools produce identical SID bytes |
| Render | Exact exported SID produces valid non-silent PCM with the intended timing and final silence |
| Transport | Play, pause, stop, current-occurrence seek, row cursor and pattern audition work natively |
| Jobs | Editing/cancelling during compile/render cannot autoplay stale audio or overwrite good output |
| PRG | Actual LOAD/RUN in VICE starts and plays the composition at the declared PAL timing |
| Package | A moved offline primary-platform bundle works without a development checkout or global SID tools |
| Guitar regression | Existing guitar documents/render path and expected baseline checks still pass |

Use one small original integration fixture for most state boundaries: one repeated pattern, a held note crossing a boundary, explicit OFF, fast retriggers, bass/drum swapping, heavy-to-mellow changes and a finite ending. Add focused malformed/oversize cases for decoder and exporter boundaries.

For the UI, verify the first-use journey:

1. Launch the packaged application and create a SID song.
2. Enter notes on each voice and hear preset auditions.
3. Change between heavy and mellow presets.
4. Loop a pattern; stop immediately.
5. Repeat a pattern, duplicate a variation, and move entries between named sections.
6. Play the whole song, pause/resume and play from a selected occurrence.
7. Edit a note, undo/redo, save, close and reopen.
8. Export SID, WAV and C64 Program.
9. Play the saved SID independently and run the PRG in VICE.

Record real GUI interaction, listening, emulator playback, compilation and automated signal checks as separate evidence. Headless/dummy checks are useful but do not establish that a human heard the result. If the execution environment cannot provide a physical listening or GUI check, finish all accessible work and identify the exact remaining check; do not relabel it as passed.

A completed implementation has a working native composition experience, an original heavy/mellow demo and real C64 exports. It does not require later wishlist features or a newly invented music driver.

## 15. Research and implementation references

These references support the chosen backend and musical architecture. The implementation requirements above are product decisions.

- R1: Lasse Oorni / Cadaver, Building a musicroutine. Phrase/order structure, musical event encoding, instrument programs and timing. [Music routine article](https://cadaver.github.io/rants/music.html)
- R2: GoatTracker author/project page. [Author's tools page](https://cadaver.github.io/tools.html) and [GoatTracker project](https://sourceforge.net/projects/goattracker2/)
- R3: Upstream-authored GoatTracker manual in the Debian source archive; sections 4.4, 5 and 6 describe the packer, player interface and GTS5 format. [GoatTracker manual](https://sources.debian.org/src/goattracker/2.76%2Bds-2/readme.txt)
- R4: GoatTracker 2.77 command-line packer source. Verify arguments against the pinned build. [gt2reloc source](https://sources.debian.org/src/goattracker/2.77%2Bds-1/src/gt2reloc.c)
- R5: Inspected GoatTracker source mirror: saver, runtime packer and player. [GTS5 saver](https://github.com/leafo/goattracker2/blob/master/src/gsong.c) ; [Music packer](https://github.com/leafo/goattracker2/blob/master/src/greloc.c) ; [C64 player](https://github.com/leafo/goattracker2/blob/master/src/player.s)
- R6: Official HVSC SID file format specification. [HVSC specification](https://www.hvsc.c64.org/download/C64Music/DOCUMENTS/SID_file_format.txt)
- R7: sidplayfp official source and upstream-authored command documentation. [sidplayfp source](https://github.com/libsidplayfp/sidplayfp) and [sidplayfp manual](https://manpages.debian.org/testing/sidplayfp/sidplayfp.1.en.html)
- R8: VICE manual. [VICE manual](https://vice-emu.sourceforge.io/vice_toc.html)
- R9: Rob Hubbard interviewed by Matthew Vosburgh, Electronics & Music Maker, April 1986. Firsthand explanation of programmable patches, software modulation and channel sharing. [1986 interview](https://www.muzines.co.uk/articles/techtalk-living-off-video/9284)
- R10: Rob Hubbard interview on C64.COM. Arrangement/channel sharing and code/data/CPU tradeoffs. [Hubbard interview](https://www.c64.com/interviews/hubbard.html)
- R11: Anthony McSweeney, The First Rob Hubbard Music Routine, 1993 author repost. Historical analysis of an early extracted routine. [McSweeney explanation](https://groups.google.com/g/comp.sys.cbm/c/heOV1Vhal6M)
- R12: James Newman, Driving the SID chip, including the citation to Hubbard's Assembly 2002 talk in Helsinki. [SID research paper](https://www.gamejournal.it/driving-the-sid-chip-assembly-language-composition-and-sound-design-for-the-c64/)
- R13: Renoise keyboard reference for familiar piano/edit conventions; this document defines gtracker's actual bindings. [Renoise keyboard shortcuts](https://tutorials.renoise.com/wiki/Keyboard_Shortcuts) and [Renoise piano entry](https://tutorials.renoise.com/wiki/Playing_Notes_with_the_Computer_Keyboard)
- R14: PAL clock/frame constants in the libsidplayfp hardware-driver documentation. [PAL timing constants](https://libsidplayfp.github.io/libsidplayfp/html/emscripten_2src_2USBSID_8h_source.html)

Historical lessons retained here are reusable musical structure, compact event state, procedural instrument behavior and explicit physical voice allocation. More elaborate compression, an original custom 6510 player, sound design controls, NTSC, MIDI and guitar conversion remain separate future work.
