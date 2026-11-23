# No Tracin’ — original 32X source-road ribbon rendering and visibility pass

**Date:** November 23, 2026. **Branch:** `improve/source-faithful-course-maps-20261122` (draft PR #3). **Owner original Sega cartridge:** U.S. *Virtua Racing Deluxe* 32X, exact SHA-256 `cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`. **Keep working from this checkpoint rather than replacing it.**

## Previous verified state

The last release had the real five 68000-source-ordered road loops, source-authenticated asphalt/height and Highland two-elevation bridge. It also had bounded, source-height-anchored terrain, distinct approximate low-polygon scenery, and offline source-road coverage masks to avoid drawing impossible source-road quads. Those systems remain intact here. The other 32X visual assets, official Sega SH-2 scenery meshes, native start/finish/AI/checkpoint semantics and full competitive races **remain unverified**.

The previous private GBA ROM is byte-for-byte reproducible from its preserved source and private generated headers: SHA-256 `d62fbe46cf702cb92e38363bff7aa6bf509d93b0d8c9234b6b741f411cb665b0`, 1,732,176 bytes, GNU ARM GCC 14.2.1.

## What changed: real source-road ribbon calculations moved to ROM

Old code in `src/vrd32x_study.c` scanned approximately 163–235 original route segments per video frame and repeatedly clamped half-widths, multiplied tangent vectors, arithmetic-shifted signed coordinates, and generated four asphalt vertices **even though these change neither with the camera nor with driving**. The old renderer then checked each segment for view visibility, submitted up to 24 asphalt quads and up to five quiet white road-edge strips.

New stdlib-only `tools/vrd32x_ribbon_mesh.py` reproduces the previous 32-bit/signed-shift geometry math **exactly at private import time**. For each source-ordered route segment it emits an immutable 32-byte ROM record: the four signed 16-bit asphalt vertices, midpoint, source visibility radius, shade, and mark flag. Original `TrackPt` is unchanged and still controls driving, vehicle physics, routes and recovery. The new renderer only reads those ROM vertices and submits the **same faces in the same order**. Original course-data headers, road/height arrays, terrain, scenery, Highland bridge data and collision code are unchanged.

Every eight neighboring source segments also receive a ROM-only conservative view-bounding sphere. The per-group radius is at least `child_radius + ceil(9*distance/7) + 16`, conservatively dominating the camera-side and depth inequalities in `r_visible()` (including fixed-point rounding). A group rejected by the same view function therefore cannot contain any individually visible road ribbon. Every segment in a group that passes this test **still gets its original per-segment visibility check**, avoiding invented culled pixels or any change to the original road geometry. Groups use no dynamic allocation or new runtime RAM.

The source-only `tests/test_vrd32x_ribbon_mesh.py` checks 4-vertex bit-exactness, signed tangent/width edge cases, test views across many camera headings and positions, rejected empty groups, bad arguments, and opt-in production C usage; `tests/test_vrd32x_visual_terrain.py` compiles and exercises the actual precomputed production renderer using synthetic noncommercial input. The original 16-sample authenticated source road remains the only physics source. Legacy/stock GBA and optional Genesis builds are not modified.

## Exact private owner-ROM verification

- Verified U.S. original Sega cartridge imported privately, at 16 samples per original lookup cell. All existing previously generated C ROM arrays (43 existing data objects) compared **identically** against the last release; 11 new ribbon arrays added. The original generated `vrd32x_study_tracks.h` remained **byte-for-byte identical**.
- Final private GNU ARM GBA: **1,764,656 bytes**; SHA-256 **`b30cb5a89b1e806756975ff02ad5b61bc8e62073eaec2a16ef42c7ae7339993f`**. Baseline: 1,732,176 bytes. Increase **32,480 ROM bytes** of precomputed road geometry and conservative groups. There is **no new EWRAM/ IWRAM array**, framebuffer, heap, or modification to GBA save formats.
- Static linked RAM in the final ELF: **26,908 bytes IWRAM** (`.data + .bss`), **228,156 bytes EWRAM**, unchanged from the earlier release. Physical GBA worst-case FPS remains unmeasured.
- Focused source/owner-cartridge tests: **19 passed**, including actual source-route/layered Highland and renderer/culling tests. A long prior local suite involving an independent soft-body stress case had not been declared a full pass; GitHub source CI is the canonical independent full source suite.

### Same-input headless mGBA 760-video-frame driving test

Same source, same original owned cartridge, GCC 14.2.1, real mGBA v0.10.x, fixed controller sequence, and **no guest RAM writes or input-data patching**:

| Course | Previous displayed FPS | New displayed FPS | Draws before / after | 32X source driving progress |
|---|---:|---:|---:|---:|
| Big Forest | 20.5117 | **20.9046** | 261 / 266 | Identical |
| Bay Bridge | 24.2054 | **24.5197** | 308 / 312 | Identical |
| Acropolis | 22.3192 | **22.3192** | 284 / 284 | Identical |
| Highland | 19.2543 | **19.7258** | 245 / 251 | Identical |
| Sand Park | 21.4547 | **21.6905** | 273 / 276 | Identical |

**Five of five original 32X source-road environments passed** boot, menu selection, source course ID checks, forward driving progress, framebuffer generation and zero emulator fatal/errors. Draw counts are video frames rendered by the GBA game per emulated video second, not guaranteed physical-console FPS. The 240-video-frame near-start comparison was **pixel-for-pixel identical (38,400/38,400 RGB pixels on each of five original road views)**, with 80 game draws in each baseline and optimized run, and identical source driving progress. Longer-session framebuffer screenshots naturally differ slightly due to differing numbers of rendered frames on the same emulated timeline.

## Owner-only rebuild and privacy

Supply **your original verified cartridge** and install GNU ARM GCC + NumPy, SciPy, scikit-image and networkx:

```bash
make vr32x-owned VR32X_ROM="/private/Virtua Racing Deluxe (USA).32x" VR32_DETAIL=16
```

Use source from this PR and the `VRD_ROM_RIBBON_MESH` private-generated header mode. The optimized GBA ROM, generated headers and test evidence are packaged privately for the owner. The original commercial 32X ROM is **never** included in the ZIP, uploaded into GitHub, or checked into the public repository. All donor-derived 32X headers stay in the owner-private release only.

## Remaining original-race work

The source-road geometry, Highland bridge, port-specific GBA visuals, and performance improvements do not supply the **original Sega SH-2 graphics meshes/textures/scenery placements** or certified original **start/finish, checkpoints, pit-routes and opponent AI**. The five import modes are playable source-geometry Free Drives, not official Sega-accurate full competitive races. Do not fabricate official race credits or mark those acceptance gates complete.
