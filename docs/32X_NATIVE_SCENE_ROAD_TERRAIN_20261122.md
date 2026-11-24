# No Tracin' — 32X original-road landscapes and scenery replacement

**Checkpoint:** November 22, 2026. **Stacked PR:** #3, branch `improve/source-faithful-course-maps-20261122`.

The user correctly reported that the 32X original-road maps looked like flat checkerboard fields. The previous 32X render path drew 5 × 5 green 256-world-unit ground tiles at zero elevation, even where the original Sega 68000 source road ran across cliffs, bridges and slopes. This milestone removes those flat tiles **in the owner-private original-ROM GBA build** and renders recognizably different landscapes around all five original road layouts.

## Source fidelity — what is real and what is reconstructed

**Original 32X cartridge data used unmodified:** all five source-ordered road centerlines, their 68000 source BSP road masks and collision heights, and Highland's independent normal and alternate overpass elevations. The private generated data is hash-gated against the exact supported U.S. ROM (`cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`).

**Not recovered original SH-2 graphics:** the original terrain display triangles, rock and tree meshes, buildings, draw order, textures, and official scenery positions are not in the reconstructed source importer. They are **new compact GBA-native interpretations**, deliberately labeled as extrapolations, and must not be mistaken for byte-identical or geographically verified Sega Deluxe scenery. The original race start/finish, checkpoints and AI still need reverse engineering.

## Changes

- `tools/vrd32x_visual_terrain.py` creates per-course 65×65 fixed u16 landscape heights from the nearest original drivable source terrain, blending smoothly into bounded mountains, coastland and desert relief beyond the actual road. Cover values are a 64×64 u8 array (grass/earth, coastal water, rocky slope, ochre sand), **not** checkerboard colors. A 5×8192-byte bitmap prevents duplicate filling of fully-covered original road quads while keeping original pit/side asphalt.
- Each course has 36–50 original-road-anchored, non-collidable scenery points in private ROM data. Compact original GBA meshes include tree clusters, fairground observation wheel, Bay harbour and bridge pylons, rock/cliff formations, Acropolis stone ruins and temple, Highland pines and Sand Park sandstone formations. All visual prop coordinates are **authored nearby**, not original Sega source mesh placements.
- `tools/import_vrd_32x_explorer.py` generates the source-anchored landscape, covers, props and skip masks only when an owned cartridge passes exact source validation; output stays under ignored `build/` and is packed into the GBA program ROM. No runtime heap or additional framebuffer/EWRAM static arrays are introduced.
- `src/vrd32x_study.c` replaces zero-elevation checkerboard tiles with 512-world-unit elevation quad mesh and per-course cover, draws source-ordered asphalt road ribbon with restrained lane-edge hints, preserves source road BSP masks/Heights for non-main road, and draws a bounded number of nearby low-poly props. Its synthetic-fixture source-only fallback remains compatible.
- `src/gba.h` and `src/render.c` add Highland and Sand Park sky/horizon palettes and distinct landscape tones, preserving existing standard and three custom tracks.
- `tools/private_32x_visual_snapshots.py` captures all five original-course early-road gameplay views using controller-only mGBA inputs, avoiding misleading screenshots after a long straight throttle drive off the source road.

## Verification

**Private actual donor:** the owner U.S. original 32X cartridge, 3,145,728 bytes, exact supported checksum. Private GNU ARM GBA artifact has **1,691,164 bytes**, SHA256 `13f5ed3609040e580695787d2ebd6010b234a17a894600c0f2c6a94da2c01c06` (final local scene build before potential source changes).

**24 focused source/ROM tests passed** locally, including live source five-ordered-coordinate route and Highland overpass tests, exact 68000 BSP constraints, and 3 new visual-terrain tests that compile the actual GBA C 3D code with synthetic assets. The standard (no original 32X) GNU ARM GBA also compiles at 348,388 bytes, SHA256 `feea873dfeb0601a27ee24f463fba0888a83c03d4d97e8711aaff185e12ea9e4`.

**All five owner-ROM GBA environments passed genuine mGBA controller-driven smoke:** 760 video frames per course, no fatal emulator errors, driving state advanced, real rendered frames observed.

| Source course | Visible GBA draws / emulated second in short test |
|---|---:|
| Big Forest | 18.78 |
| Bay Bridge | 19.57 |
| Acropolis | 16.19 |
| Highland | 16.11 |
| Sand Park | 17.68 |

This is a **performance trade-off** from the earlier bare-ground build (roughly 19–22 FPS in those five slots). It is not a hardware guaranteed frame rate, and it is not full-lap worst-case FPS. The additional rendered landscape and scenery cost 1–5 displayed FPS in these short scenarios; effects require future profiling and more efficient source-scene terrain culling. Static GBA RAM remains within the previously reported safe limits (IWRAM about 26.9 KB of 32 KB, EWRAM 228.2 KB of 256 KB). The high-level source path has no extra framebuffer or heap allocation. Road geometry/physics and saves remain governed by the previous original-source handling logic.

## Reproduce with owner's original cartridge (private)

```
PYTHONPATH=. python tools/import_vrd_32x_explorer.py \
  --rom '/path/to/owned/Virtua Racing Deluxe (USA).32x' \
  --out-dir build/vr32x-visual-source --samples-per-cell 16
make -j2 VR32_BUILD=build/vr32x-visual-source \
  build/vr32x-visual-source/notracin-vr32x-owned.gba
```

Generated `vrd32x_study_data.h`, `vrd32x_study_tracks.h`, original cartridge, and compiled donor GBA go **only** in the private owner release package, not public GitHub. Use this report as next-pass visual/performance documentation; continue from these exact source files rather than restarting with invented loops.
