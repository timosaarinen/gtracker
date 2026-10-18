-- Explicit v1 -> v2 opt-in. Beat offsets remain continuous, independent of snap.
-- Designed for the original campfire example's low string between its plucks.
gtracker.set_model("contact_0_2")
gtracker.add_slide(0.25, 6, {
  {beat = 0, fret = 0},
  {beat = 0.333333333333, fret = 2.5},
  {beat = 1.25, fret = 7}
})
