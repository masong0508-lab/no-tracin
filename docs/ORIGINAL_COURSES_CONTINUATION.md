# Original-course performance and driving validation

Checkpoint: November 21, 2026. This continues the
[first optional-course milestone](ORIGINAL_COURSES_VALIDATION.md). It improves
No Tracin's GBA import of the supplied Virtua Racing courses. The independent
[native Mega Drive physics experiment](VR_PHYSICS_RESEARCH.md) has its own
binary and validation.

## Tested build

| Property | Value |
|---|---|
| Output | `build/vr-original/notracin-vr-original.gba` |
| Size | 1,594,928 bytes |
| SHA256 | `ebe40450464e051f9b6bd928fe4bb24e7ee3c13783c8daa31b2d40e0995e0809` |
| Compiler | GNU Arm Embedded GCC 12.2.1 |
| Emulator | mGBA 0.10.5, normal emulated GBA timing |
| Source ROM SHA256 | `291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486` |

Both standard and optional configurations compile. All **35 repository tests**
pass with the supported source ROM supplied. Ten donor-dependent tests skip in
a source-only run. The tests also pass with the host C compiler's fortified
libc headers enabled. The source ROM's checksum and SHA256 remain unchanged.

The new checks cover repeated scenery submission, synthetic and actual
Acropolis rival banking, final-tick checkpoints and finishes, and the native
ROM patcher's input checks, exact edits and restoration. Existing terrain,
collision and save-compatibility tests remain in the suite.

## Exact repeated scenery is drawn once

The imported sections sometimes contain multiple copies of the same scenery.
The importer now assigns a shared identity only when the original vertex
sequence, source colour pair and source flags all match. Different source
coordinates remain distinct even when they round to the same GBA coordinates.

| Course | Stored source faces | Unique faces | Repeated entries |
|---|---:|---:|---:|
| Big Forest | 3,264 | 3,264 | 0 |
| Bay Bridge | 3,657 | 3,654 | 3 |
| Acropolis | 4,420 | 3,844 | 576 |

All source faces and section associations remain in the pack. A 556-byte
per-frame bitmap prevents submitting an identical face more than once when
overlapping sections are visible. The draw range and reserved player/effects
capacity are unchanged. This removes redundant work without deleting distinct
scenery. Synthetic tests execute the actual C submission loop, including
bitmap reset, original-coordinate distinctions and a nearly full face queue.

### Matched slow-view measurements

Each sample covers 360 native GBA frames at the same controlled Acropolis
position. The old binary is the first milestone's `0d64aa41...`; the new binary
is identified above. The final comparison reran both builds using the same
profiling setup, so node 83's old figure differs slightly from the earlier
24-position sweep.

| Acropolis route node | Previous drawn fps | Updated drawn fps | Increase |
|---|---:|---:|---:|
| 83 | 13.77051 | 19.90917 | 44.6% |
| 124 | 11.94550 | 19.07962 | 59.7% |

These are rendered frames per emulated second, not host processing speed or a
physical-hardware guarantee. The old/new screenshots were inspected for road,
car and scenery continuity. Full-lap rates below measure moving views too.
The existing painter-style renderer can still show sorting and draw-distance
artifacts; this change does not solve every visibility case.

## Six controller-driven laps

Each test boots normally, selects an original course through the menu and
drives one complete Free Run lap using controller input. The controller reads
the live car and route to choose throttle, braking and steering. It does not
write the car, route progress, race state or timer, and does not teleport or
reset the car to complete a lap. No recovery maneuver was needed.

The conservative controller targets about 27 displayed MPH. The moderate
controller caps straights at about 44 MPH and reduces its target for bends.
These are controlled continuity tests, not a claim of validation at every
speed or physics setting.

| Course | Conservative frames | Conservative fps | Moderate frames | Moderate fps |
|---|---:|---:|---:|---:|
| Big Forest | 7,196 | 19.91193 | 4,505 | 19.91359 |
| Bay Bridge | 6,958 | 19.48569 | 4,519 | 19.38930 |
| Acropolis | 9,038 | 19.79242 | 5,748 | 19.69096 |

Frames count the controller-driving interval in native GBA video frames;
they are separate from the game's own lap timer. All six runs reached the
game's next-lap state with a recorded lap time and finished with zero damage,
zero detached wheels and zero emulator errors. Their recorded input streams
were then replayed from power-on. All six reproduced the final frame count,
PC, draw count, car/race state and framebuffer exactly.

The previous binary also completed conservative laps on all three courses.
That establishes continuity before and after the changes; it is not an
assertion that every old run failed. The updated Acropolis conservative lap
averaged 19.79 fps versus 18.06 fps for that previous lap.

## Rival bank alignment

Opponents now sample the original terrain beneath all four wheel positions.
The fitted plane sets their height, pitch and roll, accounting for the actual
integer sample offsets and the renderer's rotation order. All three rival
detail levels use the roll angle. Custom-course poses retain their previous
behaviour.

Synthetic plane tests cover five headings and each detail level. Donor-backed
checks cover the steep Acropolis bank at route node 103 at three lane offsets.
An actual ARM call at the centre lane produced pitch -20 and roll -60 in
1,024-unit turn angles, matching the host result. Controlled cockpit captures
show the previous level opponent and the updated car following the camber.
The driven Acropolis lap also traverses this bank; the rival comparison itself
is a controlled scene rather than an entire completed Arcade race.

## Race-ending fixes

**Checkpoint at zero:** race logic now processes checkpoint and lap crossings
before deciding that the clock has expired. A crossing on the final clock tick
can earn its extension; a final-lap finish wins over timeout. Ordinary expiry
still ends the race, and a car cannot revive a race that already timed out.

**Finishing position:** ranking includes movement on the tick that reaches the
goal, then freezes for the result screen. A regression covers an overtake from
second to first on that tick and a subsequent opponent pass after the finish.

The transition tests execute production race code in both standard and optional
builds. They cover checkpoints, intermediate laps, final goals, timeout,
terminal-state stability and Free Run. The pre-fix logic reproduces the two
reported failures.

## Memory and remaining work

| Region | Previous linked use | Updated linked use | Capacity |
|---|---:|---:|---:|
| IWRAM code/data/BSS | 27,252 bytes | 27,340 bytes | 32,512 bytes |
| EWRAM | 228,960 bytes | 229,544 bytes | 262,144 bytes |

The EWRAM increase is the 556-byte submission bitmap plus 28 bytes for seven
rival roll values. Large data tables remain in ROM. There is no extra
framebuffer or heap. Linked IWRAM use does not measure the runtime stack's
high-water mark.

Complete Arcade races, high-speed/nitro laps, every pit entry/exit, all physics
settings and physical GBA hardware remain outside this checkpoint's coverage.
Pit-service and alternate-route race logic remain future work. The original
maps are still optional: select **VR ORIGINALS** with **L/R** on the Arcade or
Free Run course screen; **CUSTOM COURSES** is the initial selection.

## Evidence and reproduction

The downloadable validation checkpoint preserves the mGBA harness, controller
scenarios, recorded inputs, per-frame driving CSVs, JSONL output, profiles,
screenshots and replay comparisons. Paths in those session artifacts describe
the original local setup and may need adjustment on another host. They are
separate from the portable repository tests. Original ROMs and generated
commercial course headers are excluded from the evidence archive.

```sh
make test
VR_ROM="/path/to/Virtua Racing (USA).md" make test
make vr-original VR_ROM="/path/to/Virtua Racing (USA).md"
```
