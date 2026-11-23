# Original 32X Highland overpass — native 68000 alternate road restored

**Owner-verified source checkpoint: November 22, 2026, US Central.**

Do not reset the five-course reconstruction from scratch. Continue from
[PR #3](https://github.com/masong0508-lab/no-tracin/pull/3),
the [previous private eight-environment build](OWNER_32X_PLAYABLE_MILESTONE_20261122.md),
and the exact source ROM checkpoint below.

## Authenticated source discovery and repair

The verified US **Virtua Racing Deluxe (USA)** 32X ROM:
SHA256 `cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`
has a **second, independently addressable 68000 BSP collision lookup** at
alternate mode slot 3, apart from normal lookup slot 3.

The old 16-samples-per-source-cell importer rasterized only the normal
table. Its provisional closed main route occupied about **4,048 GBA world
units**, omitting most of the mode's original course geometry. The
alternate table contains a **real elevated overpass**, which joins two
valid native normal-road ramps and crosses a distinct lower-level road.

The original native data at the exact same X/Z, (7984,6608) on the
16,384-wide GBA-world scaling, demonstrate:
- Native 68000 normal road raw height **2678**
- Native 68000 alternate road raw height **5155**
- Two source collision planes, separated by **2477 native height units**

At a different upper-only coordinate (6432,6416), the native alternate
road remains drivable although the normal road has **no surface**. A
flat 2D mask union or overwriting the lower plane is incorrect.

## Implemented code

- `tools/vrd32x_layered_course.py` skeletonizes **each native source
  elevation layer independently**, checks the 68000-derived ramps against
  supported collision samples and matching source heights at every
  interpolated ramp cell, then finds the longest closed source cycle that
  includes both layers. Its raw source loop measures ~30,441 GBA world
  units; its compact 212-point GBA route measures **28,697** because
  simplification reduces tight-corner distances. **This is not a verified
  original Sega start/finish or race route.**
- `tools/import_vrd_32x_explorer.py` imports the mode-3 alternate
  stream from the verified source cartridge, emits two separate ROM-only
  height stores instead of collapsing them, uses source union in 2D map
  previews, and annotates the owner-private manifest with the two genuine
  ramp transitions and native source verification.
- `src/vrd32x_study.c` chooses the valid original upper/lower collision
  height at a crossing from the car's real previous Q8 elevation.
  Both roads remain drivable; upper-only segments remain present.
  A limited optional elevated surface is rendered above a lower-road
  driver without exceeding the existing **180-source-face** graphics
  queue or adding framebuffer, heap, or EWRAM allocations.
- `src/car.c` forwards each car's true previous elevation into the
  native layer selector during driving. Other source modes and custom
  tracks do not gain a phantom bridge.
- `tests/test_vrd32x_layered_overpass.py` includes a compiled-C
  source-only regression with **synthetic** upper/lower road tiles, and
  donor-backed source graph/both-altitudes tests when `VR32X_ROM` is
  specified. No copyrighted game bytes are committed.

## Test evidence

The original-ROM private 16-sample imported source pack contains:
- Mode 3: **212 route points**, 28,697 GBA route length, 14 genuine
  alternate ROM-height tiles, 1,559 alternate-valid sample pixels,
  18 bridge sample points in the provisional route.
- Two source-confirmed ramp links at normal/alternate pixel endpoints:
  (406,388) to (404,399) and (420,535) to (420,524). All intermediate
  joining raster samples are original road and both source height planes
  agree there.
- Underpass and overpass remain 2,477 native raw height units apart.
- Production-C native host test against a real privately generated
  exact-hash-GBA header correctly reads (7984,6608) as Q8 height
  72*256 **or** 150*256 by reference height, plus alternate-only
  (6432,6416) as 134*256. It does not leak into mode 2.
- **Five out of five 32X slots** passed short controller-driven real
  mGBA boot, course-selection, driving, advancing frames/progress,
  and no fatal/error logs in the rebuilt original-source ROM.
- The GBA was built with real GNU ARM GCC and original donor-derived
  private ROM headers, not an uncompiled demonstration.
- Short source mode3 rendered draw rate from this stage: **17.9183**
  displayed draws per emulated second. A different starting area and
  newly restored long loop mean this is **not a matched-scene FPS A/B
  comparison** against the old short loop.
- Other source mode draw rates: 0=19.0185, 1=19.8830, 2=24.8341,
  4=22.7122 in that short smoke. Hardware FPS remains untested.

## Remaining (do not misrepresent as completed)

The exact commercial Deluxe **named course assignment** for mode 3
(which resembles Highland) is still not proven by an original gameplay
trace. The 68000 uses distinct mode and track selection variables.
Original SH-2 displayed environment meshes/scenery and source
start/finish/checkpoint/pit-lane racing routes are not extracted, and
the vertical conversion is still a documented calibration estimate.

This new build is a substantially more complete native-surface
**free-drive reconstruction** and must remain labeled as such until
the above actual Sega-source race completion gates are met.

**Privacy:** Source ROM bytes, all original-derived height grids and
full imported 32X polygon assets remain exclusively in the user's
private ZIP or ignored working build directory, not in public GitHub.
