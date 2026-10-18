-- A coarse musical edit; the caller can undo this whole script in one step.
-- Scripts run as trusted local code with Lua's ordinary standard libraries.
gtracker.scale_strength(0.85)

-- Other available operations:
-- gtracker.set_tempo(84)
-- gtracker.transpose(2)             -- Shift plucked frets up two semitones.
-- gtracker.add_note(7, 1, 3, 0.55)   -- Beat, one-based string, fret, strength.
