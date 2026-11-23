# No Tracin' — exact source-road raster culling / scene performance checkpoint

**Date:** November 23, 2026. **Branch:** `improve/source-faithful-course-maps-20261122` / draft PR #3. Continue, do not reset, the [original 32X terrain milestone](32X_NATIVE_SCENE_ROAD_TERRAIN_20261122.md).

## User problem and scope

The owner asked to keep improving the newly reconstructed five *Virtua Racing Deluxe* 32X course landscapes without returning to checkerboard terrain or losing frame rate. The preceding original-source GBA already contained five course-name-verified, 68000-source ordered roads, Highland's real separate upper/lower BSP bridge, off-road height-derived GBA terrain and lightweight approximate 3D scenery. **It rendered only 16–20 FPS in short driving tests.**

This pass **preserves the same entire map, terrain elevations, road/shading algorithms, native original source routes, original bridge, car collision, and scenery objects**. It only removes hundreds of useless road corner-height and visibility tests that could never produce any raster polygon.

## Algorithm — exact 3-of-4 original-CPU road coverage gate

The optional `src/vrd32x_study.c` road renderer iterates at most 18×18 candidate 64×64 GBA world-space road patches per frame. Each patch previously called `r_visible`, queried **four** original 68000 BSP-derived sampled heights, counted supported corners, then returned without rendering if fewer than three corners were genuine source road.

`tools/vrd32x_render_candidates.py` now calculates exactly that same boolean predicate **once, offline** from private original-source `grid_samples()` data at 16- or 32-unit spacing. The resulting per-course **256×256 one-bit render eligibility map (8,192 bytes in ROM)** is stored inside the private imported `vrd32x_study_data.h`. Five course masks cost **40,960 ROM bytes**. Runtime checks one bit before calling `r_visible` or decoding any corner heights. For Highland, the eligibility gate uses **union of original normal/alternate BSP support** for a visual candidate only; upper and lower collision/height grids remain physically separate and are selected by the car's true elevation. All other course, physics, and procedural scenes are untouched.

The gate is **logically equivalent** to the original renderer's 3-of-4 corner condition, including negative/out-of-range coordinates. It cannot reject a quad that the old renderer could have submitted. As before, signed source height data stay in ROM and real original-Cart original road-and-bridge coverage is authoritative. No framebuffer, heap allocation or new IWRAM/EWRAM array is introduced. The Python gate module is stdlib-only; it imports no scientific packages, allowing GitHub source-only CI to check its exactness.

## Measured performance and playability

Both cartridges were compiled locally from the same game source and identical verified private source data with **GNU ARM EABI GCC 14.2.1**, and tested with the same Debian mGBA 0.10.x headless core, the same controller-only input timeline and no guest RAM writes. Same 760 emulated video frames per course (displayed GBA video at 59.7275 Hz). Source release was verified with original Sega U.S. 32X hash `cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`.

| Course | Previous displayed FPS | Optimized displayed FPS | Improvement |
|---|---:|---:|---:|
| Big Forest | 18.7827 | 20.5117 | +9.2% |
| Bay Bridge | 19.5686 | 24.2054 | +23.7% |
| Acropolis | 16.1893 | 22.3192 | +37.9% |
| Highland | 16.1107 | 19.2543 | +19.5% |
| Sand Park | 17.6825 | 21.4547 | +21.3% |

**Five out of five** passed controller-driven mGBA track selection, genuine frame rendering, forward progress and no emulator fatal/error log. These are *game draws per emulated video second*, **not** measured physical GBA hardware, worst-case 3D camera view or guaranteed full-course FPS.

Additional 240-video-frame early-road repeat: Big Forest 19.909→19.909, Bay Bridge 19.909→19.909, Acropolis 14.932→19.909, Highland 14.932→19.909, Sand Park 19.411→19.909 displayed FPS. This demonstrates gains also happen in the immediate original-road scenery, not merely driving offroad. The old/new 240-frame gameplay images match at roughly 94–100% of RGB pixels; small differences are expected when the engine runs different numbers of game frames during the same video time.

Production ROM-only original-source runtime: static `.data + .bss` **26,908 / 32,512 bytes IWRAM**; static `.ewram` **228,156 / 262,144 bytes EWRAM**. Visual candidate masks live exclusively in cartridge ROM.

## Testing and reproducibility

- **34/34 focused 32X source + private original cartridge tests PASSED** for five verified source routes, real Highland upper/lower bridge, 68000 native BSP boundaries, C rendering/ground collision and all new visual-culling tests.
- The four new Culling tests check both 512 and 1024 original raster sizes, fully unsupported tiles, exactly three supported corners, native Highland alternate support, end-of-world bounds, invalid data, randomized exact runtime-parity for all 256² render patch decisions, and guarantee physics APIs are unchanged.
- `tests/test_vrd32x_visual_terrain.py` maintains a synthetic private import header with the new 8192-bit gate and validates the compiled production C renderer; no commercial asset is checked into public GitHub.
- The full local host suite was **not** declared passed: one pre-existing slow soft-body arithmetic stress section interrupted local long-run testing. Source-only GitHub CI provides the canonical broad test result.
- To reproduce, run `tools/private_32x_render_benchmark.py` with an owner-private old and new GBA, headless mGBA v0.10 library, and addresses from their ARM ELF symbol tables. There is no guest RAM patching or original cartridge redistribution.

## Remaining authentic Sega original-course work

Only the original 68000 **road data and source-ordered road position** are authenticated, plus native Highland's independent overpass. Offroad landscapes and model locations remain original GBA recreations, **not** the original SH-2 display/scenery meshes. Native actual race start/finish/checkpoint records, opponents, pit AI routes and full-lap competitive 32X race playability remain separate unfinished gates. Do not claim the source-raster FPS pass completes the commercial game's five authentic competitive races.

**Privacy:** the original owned 32X Sega cartridge, its derived ROM headers, SVG/OBJ assets, and resulting GBA ROM are **never committed to public GitHub**. Only stand-alone source code, tests and this report belong in PR #3. The owner receives a separate private GBA ZIP for playtesting and local reconstruction.
