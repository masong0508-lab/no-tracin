# Original-course milestone validation

Test date: 2026-11-21. This is an optional course-data import into No Tracin's
GBA engine, using the supplied US Virtua Racing ROM. It retains No Tracin's
vehicle simulation, opponents, controls and race rules.

## Tested build

- Optional output: `build/vr-original/notracin-vr-original.gba`.
- Size: **1,571,808 bytes**.
- SHA-256: `0d64aa41d0758f7b7f4f025a6d1a453e15c8baa8fb149db2355273c00ea66fed`.
- Source revision and input identity are documented in [ROM_RESEARCH.md](ROM_RESEARCH.md).
- Compiler: GNU Arm Embedded GCC 12.2.1; build flags are in the Makefile.
- Runtime checks: mGBA 0.10.5, emulated GBA timing, default game options.
- Both the standard and optional source configurations compile successfully.

The complete source ROM is verified before import. Its SHA-256 remained
`291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486`
after all work. The importer reads that file without modifying it.

## Source and terrain verification

All three source meshes were extracted with exact section/polygon boundaries:
422 sections and 11,341 polygons in total. Native main-route graph traversal
produced 125 Big Forest, 165 Bay Bridge and 166 Acropolis nodes. Every route
closes and has consecutive original progress IDs.

The independent decoder was compared with 1,704 executions of the original
68000 height and surface routines in a Genesis Plus GX research frontend.
There were zero mismatches across the route, off-centre and cell-boundary
samples. This establishes a reference for terrain conversion, rather than
using the GBA implementation as its own sole oracle.

The final GBA binary passed **6,288 assertions across 5,833 calls to its actual
compiled ARM functions**, with zero mismatches or emulator errors. Coverage
includes all 456 main-route nodes, 912 lane samples, 396 additional offsets,
out-of-bounds queries, route positions, height/surface wrappers and centre
collision queries.

The repository's **21 tests pass with the source ROM available**. Four of
those tests are skipped in a source-only run; the other 17 require no source
ROM. Synthetic tests compile the production terrain/collision C code and use
the production integer square-root routine. Save tests exercise the real SRAM
serialization code in both stock and optional configurations, including old
records, checksums and independent original-pack invalidation.

```sh
make test
VR_ROM="/path/to/Virtua Racing (USA).md" make test
make vr-original VR_ROM="/path/to/Virtua Racing (USA).md"
```

## Integration regressions fixed

**Thin barriers.** Perimeter-only collision probes missed a solid source cell
enclosed by a larger query circle. The adapter now checks every solid 8-unit
cell intersecting the disc, using packed-byte skips for empty groups. The
Acropolis regression at `(5538, 6762)`, radius 48, now reports penetration 42
with outward normal `(-16384, 0)`. Synthetic tests also cover small body-node
contacts, tangency, corners, partial grids and recovery at map boundaries.

**Banked resets.** A horizontal soft-body spawn could put uphill wheels inside
Acropolis's banking and overturn the car. Grounded original-course resets now
align rest axes and nodes with the native slope. At route node 103 the final
car settles grounded at approximately -22.15 degrees roll, with its origin
0.02 world units above the native floor. All 24 sampled course positions
settled grounded; maximum absolute origin/terrain difference was 0.793 units.
These were controlled placements followed by normal simulation, not driven
laps through every section.

**Drawing and colours.** Converted winding and source culling flags are
preserved. Sloped ground surfaces use the renderer's surface sorting so that
a car standing on a bank remains visible. Palette matching excludes mutable
player-paint and UI slots. Colours are adapted to No Tracin's palette and haze.

**Rival clearance and saves.** Original grid/passing/wander offsets respect
the narrower imported roads. Custom and original best times have independent
SRAM blocks, and the original records include the imported pack identity.

## Rendering performance

These are rendered frames per emulated second, measured over 360 native GBA
frames after normal menu input. They are sample measurements, not guarantees
for an entire lap or a physical GBA.

| Original course | Stationary near start | Accelerating on initial straight |
| --- | ---: | ---: |
| Big Forest | 19.91 fps | 19.91 fps |
| Bay Bridge | 19.91 fps | 19.58 fps |
| Acropolis | 19.58 fps | 19.74 fps |

The 24-position view sweep found two slower Acropolis views: route node 83 at
**13.94 fps** and node 124 at **11.95 fps**. Other sampled viewpoints were
approximately 18.91-21.24 fps. The normal Big Forest Arcade acceleration sample
with all seven opponents measured **17.92 fps**.

The imported mesh submission loop runs in IWRAM. Its separate adaptive draw
range is 900-1,800 world units, with haze thresholds 550 and 850. The complete
source geometry remains in the pack; the range controls visible submission.
Maximum sampled face-queue usage was 225 of 320, with capacity reserved for
the player and effects. The current painter-style renderer and limited draw
distance can still produce visibility/pop-in artifacts.

## Race checks and remaining coverage

All three original courses were selected and driven along their initial
straights using ordinary menu/controller input. Original Big Forest was also
started in Arcade mode; all seven CPU opponents advanced and had nonzero
speed during the sample.

A separate **Big Forest-only** counter test advanced the player through
compiled `track_point` positions in 64-unit increments and called compiled
`race_step` 1,334 times. It confirmed the start crossing, three checkpoints,
three completed laps and the goal state. Artificial record changes were
restored with emulator state. This was a controlled race-state check, not a
completed physics-driven race.

Long playthroughs, every pit entry/exit, all physics-lab combinations and
physical GBA hardware remain untested. Pit geometry is included; pit service
and alternate-route race logic are future work. Source surface categories one
and three both use No Tracin's off-road behaviour. Original vehicle physics,
timing and opponents are not part of this data import.

## Memory budget

| Region | Linked use | Linker capacity |
| --- | ---: | ---: |
| IWRAM code/data/BSS | 27,252 bytes | 32,512 bytes |
| EWRAM | 228,960 bytes | 262,144 bytes |

Large mesh, route and terrain tables remain const ROM data. No extra
framebuffer or heap is allocated. Remaining IWRAM capacity also accommodates
runtime stack usage; the linked-use figure is not a measured stack high-water
mark.
