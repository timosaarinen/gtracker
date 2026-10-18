# Native runtime integration

The instrument and production chain can render without the authoring stack.
`gtracker_renderer` links only `gtracker_core` and `gtracker_production` and
uses the same continuous native renderer as the CLI/editor.

```sh
cmake --preset core-only -DGT_BUILD_RUNTIME_EXAMPLE=ON
cmake --build --preset core-only
ctest --preset core-only --output-on-failure
.local/build/core-only/bin/gtracker_runtime_example
```

Append `.exe` on Windows when invoking the executable directly. The example in
`examples/runtime_render.cpp` supplies two original notes and processes stereo
blocks. It checks/prints the output instead of opening a device or writing audio.
A game adapter sends those interleaved samples to its own PCM sink.

The C++ API is `include/gtracker/renderer.hpp`. A `NativePerformance` holds an
explicit `gt_config`, model, sorted absolute-frame events and copied slide data.
It is an in-memory interface, not a disk format or portable byte blob. Musical
authoring compiles into it; loading it never starts Lua or Python.

Construct one `NativeRenderer` for the complete scheduled performance, supplying
the instrument endpoint including its chosen decay tail and optional production
settings. `total_frames()` includes the added room tail. Call `read()` with
storage for `capacity_frames * channels()` floats, in blocks up to 65,536 frames.
It returns complete frames, zero at EOF. `reset()` rewinds the instrument seed,
events, contact and production state together. Calls on an instance are serial;
independent instances can run independently.

Construction copies schedules into the core and allocates bounded delay memory.
Reads allocate no memory and keep instrument/room state across all block
boundaries. A musical boundary does not request `reset()`. Whole-song scheduling,
cache management and PCM storage are authoring/runtime adapters above this layer.
No partial state checkpoint is provided.

Core bounds remain one million events, 16,384 slides and 65,536 total slide
points. The renderer additionally bounds instrument duration to one day and
rejects events/curves beyond that supplied endpoint. Pattern authoring retains
its smaller document and in-memory audio bounds.

Native checks compare the dry path with direct C-core PCM, change caller-owned
curves after construction, vary block sizes around musical boundaries and check
full reset, stereo tails and EOF. This verifies native integration and signal
continuity; it does not qualify a particular game's audio device or mixer.
