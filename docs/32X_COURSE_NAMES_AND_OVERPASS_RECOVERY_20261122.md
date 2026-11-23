# No Tracin' — Original 32X course identity + Highland overpass recovery

Checkpoint: November 22, 2026 (US Central). This is a continuation of
PR #3, not a new unconnected game project. Use this checkpoint together
with `OWNER_32X_PLAYABLE_MILESTONE_20261122.md` and
`ORIGINAL_32X_MODE3_ALTERNATE_BRIDGE_20261122.md`.

## Original course index correspondence proved from cartridge machine code

The user's unmodified US Virtua Racing Deluxe 32X cartridge is
3,145,728 bytes, SHA256
`cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`.

The previous reverse-engineering efforts correctly found five supported
68000 road-BSP streams, but were unable to establish whether they were
the five named commercial tracks. **That link is now source-backed.**
The original code does all of the following:

1. The menu `scene_setup_game_mode_transition` at cartridge `$E040`
   copies original player-one course selection byte `$A019` to `$FEA5`.
2. The race initialiser at `$4A70` puts `$FEA5` in D0 (course) and
   `$FEB1` in separate D1 (car type), then calls `$88D19C`.
3. The 68000 routine at `$D19C` stores D0×4 to `$C8A0` and D1×4 to
   `$C8CC`. These are **different source selection dimensions**.
4. The authentic BSP lookup at `$7406` loads `$C8A0`, doubles it again
   and uses the resulting D0×8 to address the five separate normal
   road-table pairs at source `$742C`.
5. Alternate race initialization at `$53DC` explicitly bounds course D0
   to 0..4, while three separate car-type data pages are indexed via D1.

Together with the [original Sega 32X Deluxe manual](https://www.gamesdatabase.org/Media/SYSTEM/Sega_32X/Manual/formated/Virtua_Racing_Deluxe_-_1994_-_Sega.pdf)
listing the five course selections in their game order, this establishes:

| Source 68000 BSP index | Original course name |
|---:|---|
| 0 | Big Forest |
| 1 | Bay Bridge |
| 2 | Acropolis |
| 3 | Highland (includes alternate elevated bridge stream) |
| 4 | Sand Park |

The new pure-Python `tools/vrd32x_course_identity.py` verifies **seven
actual 68000 opcode sequences** plus every source road pointer pair
before the donor importer generates named private course data. It rejects
corrupted code paths or unsupported source revisions. Synthetic regression
tests exercise each rejection, and a donor-backed test verifies every
opcode sequence against the owner's original cartridge. No original
cartridge bytes are checked into GitHub.

**Identity of original collision road layout is now established.** This
still does NOT establish Sega's actual lap start, pit route behavior,
original SH-2 track renderer/objects, course driving speeds, source
vertical unit calibration, or playable competition mode. The courses
are displayed by their names but the GBA labels its mode SOURCE DRIVE
and retains NO LAP RECORDS / START UNVERIFIED rather than fabricating
an authentic competitive race.

## Highland upper/lower bridge recovery fix

The verified alternate 68000 stream on course 3 has a physically separate
upper road at the same X/Z as its lower source road. The previous native
bridge importer and renderer selected the nearest layer using the
current car's height. But if a car fell from the elevated source road,
respawning used its *fallen* height to choose the road again. It could
reappear beneath the bridge, losing the genuine safe road location.

The GBA now retains `Car.safe_y` in Q8 world height alongside its
existing safe X/Z/heading (one runtime-only 32-bit value; no SRAM save
format change). `car_reset` records its initial road height;
`race_step` records the actual supported native road height when the
source-road free drive snapshots its safe pose. Before `car_respawn`
queries the ground after clearing Car, it seeds the native road selector
with the previously captured `safe_y`. Thus a safe upper-level pose
returns to the upper road even if the car crashes below, and an underpass
pose remains at the lower level.

A compiled production-C synthetic bridge test verifies both directions,
while the original source 32X bridge height-plane and five-mode mGBA
regressions remain in place. Stock custom-course and optional Genesis
racing gameplay is unchanged.

## Owner-private recommended GBA rebuild

```
make vr32x-owned VR32_BUILD=build/vr32x-named-original \
  VR32_DETAIL=16 VR32X_ROM='/private/Virtua Racing Deluxe (USA).32x'
```

The ROM includes the game's three custom courses plus five original-
source 32X road maps, **not the complete original Sega race events**.
Original-cartridge-derived generated headers and the built game ROM are
kept private in ignored build/, never committed to GitHub.

Next remaining original-game milestone: isolate the authentic original
start/finish positions, lap/checkpoint/progress and pit/branch route
graphs, and actual SH-2 scenery/machine-renderer assets, then test full
competitive races with original screenshot parity and measured hardware
performance. Continue here; do not reset/replace it with invented tracks.
