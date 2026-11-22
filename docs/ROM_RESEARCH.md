# No Tracin': original Virtua Racing course research

## Project direction

This is a hobby project investigating and changing game engines using the
owner's supplied ROMs. Its goals include custom driving physics, larger worlds,
and more ambitious track layouts. The original courses are an optional choice
alongside No Tracin's existing original GBA work.

The optional course pack imports original data into the GBA engine. Its latest
[performance and driving checks](ORIGINAL_COURSES_CONTINUATION.md) include six
full controller-driven laps. A separate
[native physics experiment](VR_PHYSICS_RESEARCH.md) now changes and measures
the original 68000 speed-update routine. These are two distinct outputs; a
complete native-engine overhaul remains in progress.

## Verified source

- Game: Virtua Racing, USA, product `MK-1229 -00`, region `U`.
- File size: 2,097,152 bytes.
- SHA-256: `291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486`.
- SHA-1: `ff969ae53120cc4e7cb1a8a7e47458f2eb8a2165`.
- Stored and calculated cartridge checksum: `0x95AB`.
- Reset entry point: `0x020000`.

Only this exact revision is accepted. The importer reads local bytes and never
downloads a commercial image. Generated data lives in ignored build output.

## Course data recovered

| Course | Mesh pointer table | Sections | Polygons | Main route nodes |
| --- | --- | ---: | ---: | ---: |
| Big Forest / Beginner | `0x138000` | 128 | 3,264 | 125 |
| Bay Bridge / Medium | `0x14C000` | 128 | 3,657 | 165 |
| Acropolis / Expert | `0x164000` | 166 | 4,420 | 166 |

Every section pointer and every polygon boundary is validated. Sections contain
a big-endian polygon-count-minus-one word, followed by polygons with two color
nibbles, flags and signed 16-bit vertices. A shape flag selects three or four
vertices. Coordinates are original world coordinates, not hand-traced layouts.

The 72-byte course descriptors begin at `0x035250`. Descriptor offsets `+32`,
`+36`, `+40` and `+48` point to the native X/Z nodes, headings, four-neighbour
graph and lap-progress IDs. Following forward-neighbour column one from node
zero produces closed main routes. Their progress IDs are exactly consecutive,
and all 456 main-route nodes sample as original asphalt. Other road nodes and
the pit branches remain available in the parser output.

Native physics X/Z multiplied by two gives mesh X/Z; native physics Y divided
by eight gives mesh Y. The GBA conversion uses `mesh / 4`, with 8,192 added to
X/Z. Geometry, terrain lookup, collision and route nodes use the same transform.

## Native terrain functions

The parser's formulas were recovered from these original 68000 functions:

| Entry | Role |
| --- | --- |
| `0x0220CC` | Select course mesh table |
| `0x028F78` | Load course descriptor |
| `0x027AF6` | Resolve road-reference node and lap progress |
| `0x0281D4` | Read route graph connections |
| `0x027F42` | Read packed two-bit surface category |
| `0x027C16` | Test solid surface flag during collision |
| `0x028028` | Evaluate native height plane |

The source surface categories remain distinct in parser data. Raw zero is
asphalt; raw two sets the solid collision flag. The exact names of raw one and
raw three have not been established. The GBA adapter currently treats both as
off-road terrain, retaining raw two for barrier tests.

The height and surface grids have 128 columns and course-specific row counts.
The original height tiles contain sixteen planes; the source surface tiles
pack four two-bit samples into each byte. Bounds are checked in the converter,
and out-of-map runtime surface samples are treated as solid.

GBA height queries evaluate the original plane coefficients with the original
signed division behaviour. The ground-plane adapter reports a cacheable plane
only when every contact corner addresses the same donor plane. The circuit
soft-body path queries native height separately at its contact points.
Barrier response tests the original solid cells against No Tracin's contact
circles; vehicle response continues to use No Tracin's solver.

## Integration and budgets

- The optional build has its own generated headers and object directory.
- Custom courses stay at indices 0–2; imported courses occupy 3–5.
- Original best times use a separate checksummed SRAM block. Import identity
  changes invalidate only those original records.
- Geometry and terrain tables remain `const` cartridge data.
- The added visible-section scratch list is bounded at 166 entries.
- Rendering uses the existing framebuffer and face queue, reserving 32 faces
  for the player and effects. There are no new heap allocations.
- Source face winding is converted to the GBA renderer's convention, and the
  source's backface flag is preserved.
- Source colors are mapped to the existing GBA materials, palette and haze.

## Validation evidence

The supplied ROM was booted from power-on in a headless Genesis Plus GX build
using the default SVP clock. All three original course selections were captured
at frame 3,400, and the live map pointers matched the tables above.

The Python terrain decoder was compared with **1,704 calls to the actual
original 68000 functions**, covering all main-route nodes plus off-centre and
cell-boundary samples. Height and surface results had zero mismatches.

Source-only regression tests use synthetic terrain and real No Tracin SRAM
serialization routines. ROM-backed tests are opt-in through `VR_ROM` and do not
store commercial fixtures. Runtime GBA validation and its remaining limits are
recorded in the [initial milestone](ORIGINAL_COURSES_VALIDATION.md) and
[continuation validation](ORIGINAL_COURSES_CONTINUATION.md).

## Next engine milestones

1. Extend the completed Free Run laps to full Arcade races, higher speeds,
   pit-lane entry/exit and more physics settings; improve any unstable contacts.
2. Separate route identity from nearest horizontal segment for new overpasses,
   branching shortcuts and layered roads.
3. Represent reusable stunt surfaces with explicit orientation and contacts,
   replacing the single special city-loop path where appropriate.
4. Extend segment and lap-distance formats or add sectors before expanding
   custom tracks beyond their existing byte-index limits.
5. Extend the verified native response experiment to steering and drive tables,
   deciding which modes and opponent subsystems should share each change.
6. Profile worst-case scenes and physics configurations before committing to
   frame-rate targets or larger active worlds.

## Research references

- [Virtua Racing MD Enhanced: model and SVP format notes](https://github.com/PokaMilkTea/VirtuaRacingMDEnhanced/blob/main/docs/svp-commands.md).
- [Genesis Plus GX emulator](https://github.com/ekeeke/Genesis-Plus-GX).
- [SVP development tools](https://github.com/jdesiloniz/svpdev).

The independent research frontend used for the original-ROM baseline was
`PokaMilkTea/VirtuaRacingMDEnhanced` at commit
`8004693ed487c7ad74b3f3e560780a5f3180287d`. Its host GPU enhancements are separate
from this GBA importer and are not evidence of Mega Drive hardware performance.
