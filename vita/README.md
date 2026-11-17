# Nox Map Viewer for PS Vita

First milestone of NoXCore-Vita, based on original upstream commit
`be29b2b1cca091349ab118ae8a354e9a04e983e1`. The engine and codecs come directly
from `mordentral/NoXCore`; no Mac implementation is included.

Version 0.1.3 (VPK 01.03) fixes scrolling and uses the selected 1.5× default
(640×362.67 world units on Vita's 960×544 screen). All seven zoom presets remain.
The camera retains its fractional position through the final texture sampling;
only the native-resolution scene cache has an integer origin. A single linear
filter on the completed scene gives continuous movement, with slightly softer
pixel edges than nearest-neighbour scaling. Tiles and sprite layers stay aligned
before scaling, and the HUD stays sharp at screen resolution.

The static scene cache includes a 64-unit border and a filter guard texel, so
ordinary panning reuses one composed texture. Leaving the border, changing zoom
size, switching maps or clearing caches rebuilds it. This cache is for the static
viewer: animation/gameplay will need explicit dynamic drawing or invalidation.
The analog stick has a rescaled radial dead zone and fine low-speed control;
diagonal movement is normalized. Movement uses elapsed time without artificial
camera easing or release drift. Long stalls are capped at 100 ms, and map loads,
reset and resume restart the camera clock.

Floors, edge blends, walls, window variants and initially open secret walls are
shown alongside props, doors, items, creatures and NPCs. Original upstream
`ThingArt` and `player_art` select still images, saved door angles, NPC equipment
layers and material colours. Only top-level objects are placed; inventory items
appear only when worn by an NPC. Editor markers and disabled fires are hidden.
Floor decals draw before upright objects; walls and upright objects share a
simple y sort. Full original wall occlusion, code-drawn effects, animation,
lighting, sound and gameplay are not implemented yet. The view opens at the
first PlayerStart marker.

The 0.1.1 status-logger crash fix and target-side formatting check are retained;
see [CRASH-01.00.md](CRASH-01.00.md).

Only stock `thing.bin`, `modifier.bin`, `video.idx`, `video.bag` and `.MAP` files are used. HD
packs, content packs, editors, Luau, networking and OpenGL shader plugins are
outside this target. SDL3's native GXM renderer is selected explicitly. The
application does not alter clocks or install plugins.

## Install on 3.65 Ensō

1. Install `nox-vita-viewer.vpk` with VitaShell. The title is **Nox Map Viewer**,
   title ID `NXVM00001`; it does not replace Spell Lab.
2. Copy these files from your own Nox installation:

   ```text
   ux0:data/nox/thing.bin
   ux0:data/nox/modifier.bin
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
| L / R | Zoom out / in (0.5×, 1×, 1.133×, 1.2×, 1.4×, 1.5×, 2×) |
| Cross / Circle | Next / previous installed map, alphabetically |
| Square | Return to map entry point at 1.5× |
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

The standalone CMake project compiles shared upstream asset codecs, appearance
selection helpers and this viewer. NPC pose selection was moved unchanged out
of `client/world.cpp` into `client/npc_pose.cpp` so the viewer can use it without
linking the simulation. `NOX_STOCK_ASSETS_ONLY` removes content-pack application from the
thing and modifier database loaders for this target; the desktop build keeps its existing
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
cache reuse. `--zoom 1.2` selects a scale for screenshot checks; `--zoom 1.133`
selects exact 17/15. Without `--tour`, the screenshot stays at the entry point. Timings in
these receipts are desktop CPU timings, not Vita performance.

The 0.1.2 object pass rendered WAR01A (2,592 placed objects), CON01A (1,478),
and WIZ01A (2,000) with real game assets. Version 0.1.3 adds scrolling regression
checks for dead-zone continuity, equal axial/diagonal speed, motion at 20/30/60/120
FPS, every zoom, scene rebasing, cache invalidation, map changes and reset.

```sh
cmake -S vita -B build/host-viewer -DNOX_VIEWER_TESTS=ON
cmake --build build/host-viewer -j 4
build/host-viewer/nox_scroll_test /path/to/Nox
build/host-viewer/nox_scroll_test /path/to/Nox --gpu
```

Both software and accelerated Metal checks passed. SDL's software copy path
truncates fractional source coordinates, so only `--gpu` tests subpixel image
motion. At every zoom, eight successive 1/8-world-pixel moves changed the GPU
image without a scene rebuild. Rebuilding at the same world view gave identical
pixels at 1.5× (and mean channel error below 0.002/255 across all presets).
A three-second, 180-frame pan rebuilt the scene 12 times. These are desktop
correctness/draw-count checks, not Vita FPS measurements. Vita compilation,
ELF/SELF conversion and VPK validation also passed; hardware scrolling and FPS
still need a retest. The user reported low FPS and judder with 0.1.2.

## Memory and measurements

- Newlib heap reservation: 128 MiB.
- Retained RGBA texture payload: at most 32 MiB (GXM row alignment included).
- Scene target: one padded viewport, resized on zoom; RGBA payload about
  1.46 MiB at default 1.5×, up to 9.56 MiB at 0.5×, plus GXM depth/target allocations.
- Retained decoded images: at most 8 MiB.
- Retained bag segments: at most 4 MiB. A larger individual segment may be decoded
  temporarily, but is dropped after that request.
- Map changes clear the caches. Transient decoding buffers, the map, thing database,
  container overhead, the framebuffer and SDL's renderer allocations are additional.

The HUD scene counts describe the padded cached view, and uploads count only
the current frame. The HUD separates texture payload from free OS pools. SDL's GXM backend reserves
a CDRAM texture pool internally, so low reported free CDRAM is not equivalent to
the viewer having filled its texture cache. The displayed work time covers input,
map drawing and texture uploads before the HUD and presentation; it is not a GPU
timer. FPS includes presentation and pacing. Record a warm view, a cold pan and a
map transition on the actual Vita before drawing performance conclusions.

Next milestones: hardware performance validation, animation and lighting, then a single
offline playable scene using stock assets.
