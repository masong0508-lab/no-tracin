# Virtua Racing Deluxe (USA) — all five original ordered road-coordinate loops

Source and private GBA milestone: November 22, 2026 (US Central), continuing
No Tracin' draft PR #3. This supersedes the provisional skeleton loops in the
five 32X Free Drives. It is not a claim of authentic Sega checkpoint/start
logic or original 32X SH-2 scenic polygon meshes.

## Evidence from the exact owner-provided cartridge

Original source image: 3,145,728 bytes, SHA-256
`cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`.
The original game menu copies course selection ($A019) to $FEA5; the 1P race
initializer loads this value as course D0 at ROM `$4A70`, invokes
`game_mode_track_config` at `$D19C`, stores D0×4 at RAM `$C8A0`, and the
source road collision lookup at ROM `$7406` indexes the native normal BSP
pointer table at `$742C` by this selected course. The distinct car selection
D1 and game-state/track variables must not be mistaken for the course ID.
The five names follow the original U.S. Deluxe manual course order.

At physical ROM offset `$018A04` (68000 `$898A04`), there are five
20-byte course-scene records; each entry's first longword points to a stream
of **ordered signed big-endian 16-bit X/Z coordinates**. Its first path is a
closed original main road loop; its terminator returns within 512 original
units of the first node, after which a jump of more than 1200 units introduces
additional branch/control-point data. The original source route is verified
against its own 68000 BSP collision surface at **every vertex and three
quarter-chord points per route segment**. This rejects false shortcuts across
voids/off-road. Signed native X/Z coordinates map to GBA world (x+16384)/2.

| Original 32X course | 68000 source pointer | Ordered original nodes | GBA main-loop length |
|---|---:|---:|---:|
| Big Forest | `$009641FC` | 163 | 28,035 |
| Bay Bridge | `$00967294` | 199 | 27,161 |
| Acropolis | `$0096A2D4` | 235 | 35,707 |
| Highland | `$0096D282` | 203 | 28,552 |
| Sand Park | `$0094926C` | 191 | 43,579 |

All five loops remain below GBA's 254-segment route-index safety cap and
65,535-unit lap length representation. The importer retains the original
vertex ordering instead of skeletonizing the visual mask. Highland route
points select original alternate raised BSP road collision when supported,
keeping the bridge separate from its original lower underpass. The associated
material/scene palette is a **GBA approximation using existing resources**,
not extracted source SH-2 scenery.

## Private build and playback

Build locally, with the owner's original cartridge outside GitHub:

```
make vr32x-owned VR32_BUILD=build/vr32x-native-route VR32_DETAIL=16 \
  VR32X_ROM='/private/Virtua Racing Deluxe (USA).32x' -j2
```

Donor-backed tests:

```
VR32X_ROM='/private/Virtua Racing Deluxe (USA).32x' \
  python3 -m unittest discover -s tests -p 'test_vrd32x_course_identity.py' -v
VR32X_ROM='/private/Virtua Racing Deluxe (USA).32x' \
  python3 -m unittest discover -s tests -p 'test_vrd32x_native_routes.py' -v
```

Actual tested native-ordered-route build: 1,581,884 bytes (GNU ARM GBA).
Five controller-driven mGBA smoke runs passed (760 emulated video frames
per slot, no emulator errors). Short displayed frame rate (FPS) by scene:
0=19.96; 1=21.38; 2=21.77; 3=18.78; 4=20.83. Those are short driving
samples, not full-track/hardware frame rates.

## Completion gates (do not erase this list)

- The recovered original scene path **does not establish Sega's official
  starting grid, checkpoint order, pit or branch eligibility, AI policy,
  finish-line detection or lap score**. No invented competitive credits.
- The 68000 BSP data is collision/elevation data. **Original 32X SH-2
  rendered scenery/landmarks/texture meshes remain unported**. Existing
  lightweight GBA scene palette and low-poly visual assets are approximations.
- The height conversion factor (`HEIGHT_DIV=32`) remains an empirical
  GBA calibration rather than a verified native physical unit scale.
- Full-length controller playthroughs, match-captured source-console
  visual comparisons, frame-rate floors, and physical GBA tests remain.
- Optional Genesis/SVP original-course support remains independent and
  needs the separate user-owned original Genesis cartridge, not this 32X ROM.

Generated proprietary header, source-derived SVG/OBJ and compiled
cartridge-containing GBA game stay in the owner's private package/ignored
`build/`. Public GitHub includes only importer code, synthetic tests and
this implementation/provenance report. **Continue this milestone; do not
restart the source mapping investigation.**

## Course-select map animation (source road, not an invented start)

The GBA course selector now draws a 3×3 moving location marker along the
**verified ordered source road nodes**, projected into the same authentic
68000 BSP-derived road-mask extent as the background map. The marker does
not draw a replacement centerline across bridges or pit branches and does
not imply that vertex zero is the original race start/finish. The 1-bit
source-derived silhouette remains cached in ROM; only nine tiny marker
pixels are dynamic. Production-C synthetic regression verifies map
orientation and border clipping; five user-ROM-derived GBA mGBA controller
runs passed after the change.
