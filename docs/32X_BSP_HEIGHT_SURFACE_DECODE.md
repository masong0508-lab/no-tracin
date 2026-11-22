# Virtua Racing Deluxe 32X: authentic BSP road boundary and plane-height decoding

**Milestone: November 22, 2026 — source verified, not a playable five-course import.**

The 1994 US Virtua Racing Deluxe cartridge was independently verified by
SHA-256 (`cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`).
All data inspection is read-only. No ROM bytes or exported meshes are checked
into GitHub. The GBA racing engine, options, SRAM, and existing ROM files are
not modified in this milestone.

## What the 28-byte groups actually are

Earlier research established six normal 64×64 sparse road lookup tables, one
alternate table and 1,770 count-delimited records. Each non-background record
has exactly `N+1` 28-byte groups, followed by a shared bank-end marker.

We can now identify the groups using **actual Sega 68000 instructions**, not
statistical guesses. At ROM file `$00748C`, `angle_normalize_bsp` reads a
count, then processes successive **four-constraint BSP boundary groups**.
At `$0075C8`, `plane_eval` reads the coefficients for the road/surface plane.
These two routines are called from the 32X player's four-point collision and
terrain-height probes.

| 28-byte group offset | Confirmed purpose |
|---|---|
| `+$00` (word) | Packed 4×3-bit selector/skip tests; low four bits unused by those tests |
| `+$02..+$11` (8 words) | Four pairs of either direct thresholds or signed slope/intercept terms |
| `+$12` (signed word) | Road-plane coefficient for normalized world X |
| `+$14` (signed word) | Road-plane coefficient for normalized world Z |
| `+$16` (signed word) | Road-plane constant term |
| `+$18` (word) | Other metadata, with **low-byte sign bit** selecting plane evaluation precision |
| `+$1A` (word) | Additional metadata — meaning unverified; not treated as a vertex |

One source tile covers a 512×512 region of the 68000's world plane. The
source first normalizes both world coordinates to two-word local values:

```
X = ((world_x + 0x4000) & 0x01ff) * 2   # 0..1022
Z = ((world_z + 0x4000) & 0x01ff) * 2
```

Each BSP constraint either skips the current group or continues to the next
constraint. A group selected after all four constraints is passed to the
surface-height routine, which uses signed 68000 multiplications and an
arithmetic right shift:

```
raw_height = (X * signed_word_9 + Z * signed_word_10
              + 32 * signed_word_11) >> shift
shift = 5 if low byte of word_12 is negative, else 6
```

The runtime interpreter 
[`tools/decode_vrd_32x_surface.py`](../tools/decode_vrd_32x_surface.py)
implements all four original selector branches and the original signed
integer arithmetic. It fails closed on unsupported slot 5, invalid ROM
identity, corrupt pointer ranges, and impossible counted-record lengths.
The separate sampled OBJ exporter does not claim the original SH-2 mesh.

## Cross-tile road and height continuity

The five verified normal slots were sampled at **five locations along every
adjacent pair of active lookup cells**, one world unit apart across a border.
This tests actual neighboring source records rather than assuming the first
word after the header is a coordinate.

| Slot | Shared valid edges | Road-classification disagreements | Median height delta | 99th percentile delta | Maximum delta |
|---:|---:|---:|---:|---:|---:|
| 0 | 1,689 | 5 | 3 | 19 | 49 |
| 1 | 1,832 | 8 | 4 | 17 | 21 |
| 2 | 1,563 | 4 | 4 | 17 | 35 |
| 3 | 1,262 | 2 | 4 | 17 | 18 |
| 4 | 3,153 | 7 | 0 | 21 | 39 |

Across 9,499 adjacent on-road pairs, there were only 26 cases where the two
positions differed in region classification. The height samples are close
at those borders, strong independent evidence of the BSP and height-plane
interpretation. It does **not** identify the game's display-specific
height-unit scale, course name per slot, alternate routes, scenery, or
multi-level collision behavior.

An additional alternate slot 3 stream has 46 shared valid edge samples,
zero classification disagreements and a maximum height delta of 16.

## Private 3D reconstructed study surfaces

`tools/export_vrd_32x_surface_mesh.py` takes the ROM's actual BSP and height
planes and samples each active tile on an 8×8 grid. It then triangulates
only quads with four valid on-road corners. No nonexistent off-road triangles
are invented. The resulting **sampled surfaces are NOT the original 32X
vertex meshes**, are not grouped by named track, and do not contain Sega's
original graphical model geometry or props.

| Slot | Valid sampled road vertices | Triangles in study surface |
|---:|---:|---:|
| 0 | 10,930 | 19,632 |
| 1 | 11,906 | 21,136 |
| 2 | 9,973 | 16,936 |
| 3 | 8,093 | 14,028 |
| 4 | 20,561 | 37,244 |
| **Total** | **61,463** | **108,976** |

The OBJ vertical scaling default is `raw_height / 32` for convenient
inspection, **not a validated GBA↔32X conversion**. The X/Z study scale
is original 32X coordinates divided by eight. Only tools, tests and
noncommercial documentation are committed; all user-ROM-derived OBJ, GLB,
heightmap PNG and other asset files remain private delivery artifacts.

## Source evidence and tests

The publicly available ROM disassembly provides the relevant code:

- [angle_normalize_bsp.asm](https://github.com/matiaszanolli/sega-vr-disasm/blob/master/disasm/modules/68k/math/angle_normalize_bsp.asm)
- [plane_eval_pair.asm](https://github.com/matiaszanolli/sega-vr-disasm/blob/master/disasm/modules/68k/math/plane_eval_pair.asm)
- [track_boundary_collision_detection.asm](https://github.com/matiaszanolli/sega-vr-disasm/blob/master/disasm/modules/68k/game/collision/track_boundary_collision_detection.asm)
- [directional_collision_probe.asm](https://github.com/matiaszanolli/sega-vr-disasm/blob/master/disasm/modules/68k/game/collision/directional_collision_probe.asm)

Tests verify every BSP branch with hand-constructed synthetic inputs,
signed height, alternate precision, two-group records, invalid inputs,
original ROM identity, exact known per-slot surface samples, boundary
continuity across five normal slots and export size/triangle determinism.
Existing No Tracin' source and ROM-dependent tests still run separately.

The next major milestone is mapping lookup slots to the five named Deluxe
courses, reconstructing *actual drivable progression, pit/branch graph,
start and finish*, and integrating the validated height field with GBA
collision/rendering budgets. No new 32X race is claimed playable yet.
