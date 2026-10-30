# Linux candidate

Linux desktop packages contain the SID and guitar editors, CLI, capture tool,
original scores, and the same pinned SID synthesis helpers as Windows.
Extract the complete ZIP with an extractor that preserves executable permissions,
then run `./start-sid.sh` or `./start-guitar.sh`. Nothing installs system-wide.
Save songs outside the extracted folder; `--data-dir PATH` selects application data.

The package uses system C/C++ runtimes and the SDL 1.2 ABI for the external
GoatTracker packer. On Ubuntu, install `libsdl1.2debian`; newer distributions may
provide that ABI through SDL 1.2 compatibility packages. Windowing and sound need
the distribution's X11/Wayland and PulseAudio/ALSA runtime libraries. FFmpeg is
optional for MP4 export (`sudo apt install ffmpeg fonts-dejavu-core`).
The Windows download helper is not used on Linux.

## Build from source

Use Python 3.10+, CMake 3.25+, Ninja and a C++17 compiler. Ubuntu build dependencies:

```sh
sudo apt install build-essential ninja-build pkg-config autoconf automake libtool \
  libsdl1.2-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxrender-dev libxss-dev libwayland-dev libxkbcommon-dev \
  libasound2-dev libpulse-dev libgl1-mesa-dev ffmpeg fonts-dejavu-core
python3 tools/dev.py doctor
python3 tools/sid_backend.py prepare
python3 tools/dev.py dev
```

`bun run dev` is equivalent when Bun is installed. SID preparation verifies pinned
source archives, builds native helpers, and links the pinned emulator libraries
statically. The system SDL 1.2 library remains dynamically linked and replaceable.
Generated builds and caches stay under `.local/`.

## Verify and package

```sh
python3 tools/dev.py ci --if-due
python3 tools/dev.py build --profile desktop
python3 tools/dev.py test --profile desktop
python3 tools/sid_stream_verify.py
python3 tools/dev.py package --profile desktop
python3 tools/verify_package.py .local/packages/gtracker-desktop-Linux-x86_64.zip
```

The package check executes trusted packaged binaries in a separate Unicode/space
path, checks payload hashes, exports SID/PRG/WAV/MP4, launches both editors with
dummy devices, and verifies that installation files stayed unchanged. Evidence
lives in `.local/package-checks/`. It is not a listening or physical-device test.
The manual GitHub qualification workflow also builds a Linux candidate; artifacts
are not automatically published as releases.

For WSL2, use a separate checkout on the Linux filesystem. Do not share `.local`
build trees with Windows: CMake caches and helper binaries are platform-specific.
WSLg can expose display and sound, but dummy tests do not validate those devices.
Native desktop listening, microphone capture and clean-distribution testing remain
release gates. See [release readiness](release.md) for current evidence.
