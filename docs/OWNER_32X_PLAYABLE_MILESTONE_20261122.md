# Owner-verified 32X playable source-road release — November 22, 2026

## This is the durable checkpoint. Do not reset the milestone.

**Source branch:** `improve/source-faithful-course-maps-20261122`, stacked
draft [PR #3](https://github.com/masong0508-lab/no-tracin/pull/3)
on draft PR #2. This checkpoint is a playable **eight-environment**
owner-private GBA based on the exact supported U.S. *Virtua Racing Deluxe*
32X cartridge. The ROM-derived private headers and commercial cartridge
are deliberately excluded from public GitHub.

**Exact verified donor cartridge:**
3,145,728 bytes; SHA-256
`cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`.

The user explicitly wants the original maps faithfully reconstructed
rather than fabricated road-study race routes, and does not want the
project repeatedly restarted. Future work MUST continue from these commits,
artifacts and outstanding authenticity gates, not start from scratch.

## What has actually been built and tested

`make vr32x-owned VR32X_ROM="/private/Virtua Racing Deluxe (USA).32x" VR32_DETAIL=16`
builds three original No Tracin' custom circuits plus five ROM-derived
32X source-road Free Drives **without** the separate Genesis/SVP donor.
Use GNU ARM GCC 14.2.1 and Python dependencies networkx, NumPy, SciPy,
scikit-image. Free drives use track IDs 3–7; L/R switches from CUSTOM
COURSES to 32X ROAD STUDIES on the course menu. Each mode is FREE DRIVE:
unverified provisional staging cycles are NOT used to award fake laps,
racing records, start lines, or course names.

- Recommended 16-samples-per-original-cell collision raster:
  1,564,152-byte GBA, SHA-256
  `196cf1a598a87ca9994d2181406d4876df69a388823824a4e84b1c58b31a22e2`.
- Experimental `VR32_EXACT=1` native 68000 source-priority collision-triangle
  visual overlay: 2,403,832-byte GBA, SHA-256
  `d4308cf945ebeb41dd4bae95ebd843a53cd9c0ce31b214b874e9f5785104ff4e`.
  This is **collision geometry**, not authentic Sega SH-2 scenery meshes;
  it was slower in some measured views and is **not the default**.
- Both built using real GNU ARM EABI GCC, not merely a Clang prototype.
  Static IWRAM 26,904/32,512 bytes; EWRAM 228,156/262,144 bytes.
  No extra framebuffer or heap allocation.
- Donor-backed exact 32X source parser/BSP focused tests: **14 passed**.
  The comprehensive host suite was interrupted by a long-running,
  pre-existing soft-body arithmetic stress case, so **do not call the
  complete donor suite a pass**.
- Local real mGBA 0.10.x controller-driven smoke (760 emulated video
  frames per mode), zero mGBA fatal/error events, all five correct
  source mode IDs selected, frames rendered, driving state advanced:

| Source mode | GBA track ID | High-detail drawn FPS | BSP overlay FPS |
|---:|---:|---:|---:|
| 0 | 3 | 20.28 | 17.29 |
| 1 | 4 | 20.75 | 20.59 |
| 2 | 5 | 25.07 | 23.18 |
| 3 | 6 | 26.25 | 26.09 |
| 4 | 7 | 23.26 | 22.63 |

FPS = displayed game draws per emulated GBA second in that short
controller test; NOT long-session minima, host rendering speed, hardware
guarantee or source 32X video FPS. Source-emulator smoke does not prove
a complete authentic Sega race is drivable.

## Authentic original 32X geometry evidence and private outputs

The 68000 source BSP half-plane fields and signed height plane are decoded
in `tools/vrd32x_exact_bsp.py`. The owner-private exporter
`make vr32x-original-bsp-evidence VR32X_ROM="/private/original.32x"`
writes five SVG collision-area silhouettes, five priority-resolved OBJ
source collision meshes, and a manifest in `build/vrd32x-bsp/`.
These reproduce source **collision plane geometry**, not SH-2 display
triangles or named race progression. They remain private donor derivatives.

`tools/import_vrd_32x_explorer.py --samples-per-cell 16` embeds denser
16x16 sample tiles in **private ROM-only** source headers (8x8 legacy
remains available, unchanged default); the runtime in
`src/vrd32x_study.c` chooses generated sample shifts.
`--exact-mesh` optionally emits private priority-resolved near road
triangles, with a cap of 100 near and 180 total sourced faces. The
normal 16-sample source collision/height mode is the speed-preserving
recommended version.

The independently confirmed three Genesis/SVP original courses (Big
Forest, Bay Bridge, Acropolis) still exist in the **separate** optional
`vr-original` / `vr-deluxe-study` paths and need the other, previously
supplied Genesis original. **They were not discarded or overwritten.**

## Remaining authenticity gates — not claimed completed

The user's request for *faithful original named Deluxe races* remains
open despite having a playable original-source road subset. Needed:
1. Correct 68000 lookup mode to five named *Virtua Racing Deluxe* course
   identity verified by original 32X emulator/SH-2 gameplay traces.
   Don't assume source mode indices 0–4 are names.
2. Decode original 32X SH-2 scenery/render mesh, vehicles, textures,
   start grids, checkpoints, pit and branching route graph.
3. Verify original 32X road elevation unit scale and multi-level
   overpass/tunnel collision layers against cartridge gameplay captures.
4. Implement authenticated competitive race routing and full-lap
   original-map controller playthroughs, then benchmark GBA FPS
   and memory. No fictional laps or invented bridges.
5. Integrate verified source assets with bounded GBA polygon budget,
   preserve original game controls, save data and the separate Genesis
   importer.

The owner-private release ZIP was handed to the user through the
conversation. The GitHub repository intentionally contains **only
source, tests and this no-reset record**, never the original cartridge,
generated commercial ROM-derived headers, compiled donor-containing
GBA, SVG or OBJ files. Rebuild locally with the owner donor.
