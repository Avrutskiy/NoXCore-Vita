# Nox Map Viewer for PS Vita

First milestone of NoXCore-Vita, based on original upstream commit
`be29b2b1cca091349ab118ae8a354e9a04e983e1`. The engine and codecs come directly
from `mordentral/NoXCore`; no Mac implementation is included.

Version 0.1.1 (VPK 01.01) fixes a crash in the once-per-second status logger.
Vita newlib did not consume `%zu` arguments, causing the following `%s` to treat
the texture byte count as an address. Both initial hardware dumps resolve to
`strlen` called from `_svfprintf_r` with that exact count. The viewer now uses
supported unsigned formats, runs a formatting check on the target at startup,
and retains line information in the host ELF for future crash diagnosis.

This build renders the original map's floors, edge blends and walls, including
window variants and initially open secret walls. It opens at the map's first
PlayerStart marker when one exists. It is a geometry viewer: objects, actors,
animation, lighting, sound and gameplay are not implemented yet.

Only stock `thing.bin`, `video.idx`, `video.bag` and `.MAP` files are used. HD
packs, content packs, editors, Luau, networking and OpenGL shader plugins are
outside this target. SDL3's native GXM renderer is selected explicitly. The
application does not alter clocks or install plugins.

## Install on 3.65 Ensō

1. Install `nox-vita-viewer.vpk` with VitaShell. The title is **Nox Map Viewer**,
   title ID `NXVM00001`; it does not replace Spell Lab.
2. Copy these files from your own Nox installation:

   ```text
   ux0:data/nox/thing.bin
   ux0:data/nox/video.idx
   ux0:data/nox/video.bag
   ux0:data/nox/maps/WAR01A/WAR01A.MAP
   ```

   Copy other `maps/<name>/<name>.MAP` folders to browse them too. File lookup
   accepts the original uppercase or lowercase names. Game assets are not included
   in the VPK or this repository.
3. Launch the viewer. It starts with `WAR01A`. Missing data is shown on screen;
   diagnostics are written to `ux0:data/nox-vita/viewer.log` (replaced each launch).

| Vita control | Action |
| --- | --- |
| Left stick / D-pad | Pan |
| L / R | Zoom out / in (0.5×, 1×, 2×) |
| Cross / Circle | Next / previous installed map, alphabetically |
| Square | Return to map entry point at 1× |
| Triangle | Clear image caches for a cold-load comparison |
| Start + Select | Exit |

Desktop controls: WASD/arrows, +/- zoom, N/P maps, R reset, C clear cache, Esc exit.

## Build

Requires CMake 3.25+, Ninja, Python 3.11+ for the convenience driver, and VitaSDK
with its C++20 compiler. Development used VitaSDK GCC 15.2.0 and SDL 3.4.16.
Use the same SDK ABI for SDL and the viewer; this target builds SDL from source
when `NOX_SDL_SOURCE` is supplied. Source release:
[SDL 3.4.16](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16).
The upstream archive's expected SHA-256 is
`7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68`.

```sh
export VITASDK=/absolute/path/to/vitasdk
python3 vita/build.py --sdl-source /absolute/path/to/SDL3-3.4.16
```

Alternatively, configure directly from this repository:

```sh
cmake -S vita -B build/vita-viewer -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DNOX_SDL_SOURCE=/absolute/path/to/SDL3-3.4.16
cmake --build build/vita-viewer -j 4
```

The output is `build/vita-viewer/nox-vita-viewer.vpk`. The Python driver also
checks ZIP CRCs, records hashes and writes `build/vita-viewer/build-receipt.json`.
It never downloads or copies game data. If SDL3 is already installed in VitaSDK,
omit `--sdl-source` and CMake will use its package configuration instead.

The standalone CMake project compiles the shared upstream asset codecs and this
viewer only. `NOX_STOCK_ASSETS_ONLY` removes content-pack application from the
thing database loader for this target; the desktop build keeps its existing
behavior. The hash cast in `util/text.hpp` makes the existing 32-bit truncation
explicit without changing the hash result.

## Desktop verification

Use an installed SDL3 CMake package, or pass `NOX_SDL_SOURCE` to build SDL locally:

```sh
cmake -S vita -B build/host-viewer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-viewer -j 4
build/host-viewer/nox_vita_viewer --game-dir /path/to/Nox \
  --map maps/WAR01A/WAR01A.MAP --headless build/war01a.png --frames 180 --tour
```

Headless mode creates no window and uses SDL's software renderer. It saves the
960×544 image and an adjacent JSON receipt. `--tour` moves the camera to exercise
cache reuse. Without it, the screenshot stays at the entry point. Timings in
these receipts are desktop CPU timings, not Vita performance.

The initial build has rendered WAR01A, CON01A and WIZ01A with real game assets.
Vita cross-compilation, ELF/SELF conversion and VPK generation have passed.
The initial version loaded and displayed WAR01A on hardware, then crashed in its
status logger. Version 0.1.1 still needs a hardware retest; sustained frame rate,
controls and suspend/resume are **not yet verified**.

## Memory and measurements

- Newlib heap reservation: 128 MiB.
- Retained RGBA texture payload: at most 32 MiB (GXM row alignment included).
- Retained decoded images: at most 8 MiB.
- Retained bag segments: at most 4 MiB. A larger individual segment may be decoded
  temporarily, but is dropped after that request.
- Map changes clear the caches. Transient decoding buffers, the map, thing database,
  container overhead, the framebuffer and SDL's renderer allocations are additional.

The HUD separates texture payload from free OS pools. SDL's GXM backend reserves
a CDRAM texture pool internally, so low reported free CDRAM is not equivalent to
the viewer having filled its texture cache. The displayed work time covers input,
map drawing and texture uploads before the HUD and presentation; it is not a GPU
timer. FPS includes presentation and pacing. Record a warm view, a cold pan and a
map transition on the actual Vita before drawing performance conclusions.

Next milestones: hardware validation, static object rendering, then a single
offline playable scene using stock assets.
