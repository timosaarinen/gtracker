# Third-party notices

The native dependencies are fetched from pinned source archives; their terms are
reproduced below. Preserve the applicable notices when packaging binaries. Project code is MIT (see LICENSE), except explicitly licensed external helper
sources. These notices preserve the separate dependency terms.

## SDL 3.2.28

Source: https://github.com/libsdl-org/SDL/tree/release-3.2.28

```text
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## Dear ImGui 1.91.9b

Source: https://github.com/ocornut/imgui/tree/v1.91.9b

```text
The MIT License (MIT)

Copyright (c) 2014-2025 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## ImPlot 0.16

Source: https://github.com/epezent/implot/tree/v0.16

```text
MIT License

Copyright (c) 2020 Evan Pezent

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## nlohmann/json 3.11.3

Source: https://github.com/nlohmann/json/tree/v3.11.3

```text
MIT License

Copyright (c) 2013-2022 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Lua 5.5.1

Source: https://www.lua.org/ftp/lua-5.5.1.tar.gz (src/lua.h)

```text
Copyright (C) 1994-2026 Lua.org, PUC-Rio.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
# SID backend tools

SID mode invokes separately distributed executables; it does not link GPL SID
libraries into the gtracker application or guitar core.

- GoatTracker 2.77 by Lasse Oorni and contributors: GPL-2.0-or-later for the
  packer/editor. Its player source and song-binary exception are described in
  the upstream readme; do not conflate that exception with the executable license.
  Source archive SHA-256 and the console-entry/diagnostic patch are in
  `tools/sid_backend.py`.
- libsidplayfp 3.1.1 and libresidfp 1.2.3: GPL-2.0-or-later. The separate
  `gtracker_sidrender` adapter (`tools/sid_render.cpp`) is GPL-2.0-or-later.
  reSIDfp table-construction tasks are serialized for deterministic results;
  source versions, checksums and the complete patch recipe are preserved.
- SDL 1.2, commit ef3a6c05, used by the external packer: LGPL-2.1-or-later.
  Windows backend preparation copies the MSYS2 i686 SDL DLL. Its source archive
  and license travel in the developer package; it remains replaceable.
- MinGW-w64 winpthreads runtime: its permissive notices are included beside the
  DLL as `winpthreads-COPYING.txt`.

The Windows developer bundle includes the required backend binaries, corresponding
source archives, adapter source, recipes, checksum manifest and applicable license
texts under `sid-tools/`. No binaries, ROMs or generated audio are committed to
Git. The optional upstream sidplayfp 3.2.1 console download is an investigation
tool; it is not the application's runtime renderer or part of the bundle.

The separate realtime helper `tools/sid_stream.cpp` is GPL-2.0-or-later and
uses the same pinned libsidplayfp/reSIDfp libraries. Its source, recipe and
corresponding library archives accompany the helper in packages. FFmpeg is an
optional external executable for MP4 export and is not bundled. The Windows
setup helper downloads Gyan's FFmpeg 9.0.2 essentials build on request, retaining
its LICENSE and README.txt alongside the executable. This GPL build retains its
upstream terms; gtracker's MIT license does not relicense it. Download provenance
and hashes are recorded in download.json in the installation folder.
