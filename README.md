# No Tracin'

A stunt-driving game for the Game Boy Advance: Race Drivin' (Genesis) mixed with Payback (GBA) and Virtua Racing. You drive a flat-shaded 3D city, Freedom City, with a Stunt Park (a loop, a banked curve and a canal jump), street ramps, tricks, stars to find and cheat codes. The Virtua Edition adds three race circuits in the style of Virtua Racing's courses, an arcade race against seven CPU cars, and a Virtua-style menu.

The standard `notracin.gba` build uses this project's own code and assets. The optional original-course build described below imports course data from a locally supplied Virtua Racing ROM. Source ROMs and generated imported assets are kept outside the tracked source tree.

## Latest owner-original 32X source-road build (Nov 22, 2026)

**No reset necessary:** the five named Deluxe courses now use their **original
ordered 68000 source road coordinates**, not synthetic road-study skeleton
loops. The verified cartridge links the source course index to Big Forest
(163 nodes), Bay Bridge (199), Acropolis (235), Highland (203) and Sand Park
(191). Highland also preserves the original raised alternate BSP bridge as
a separate driving height above its underpass. Source-road map previews
show accurate asphalt silhouettes with a moving location dot.

Build the owner-only standalone 8-environment GBA ROM using an unmodified
verified US Sega 32X cartridge, kept outside the public Git repository:

```sh
make vr32x-owned VR32_BUILD=build/vr32x-native-route VR32_DETAIL=16 \
  VR32X_ROM='/private/Virtua Racing Deluxe (USA).32x' -j2
```

At game selection press L/R to choose the original 32X road set, then A to
Free Drive. The original Sega official race starts, laps, checkpoint/AI
logic, and SH-2 scenery have **not** been reconstructed, so this build
keeps source Free Drive distinct from the completed custom arcade races.
The existing historical "road studies" sections later in this README
describe earlier research, not the current restored source-coordinate mode.
See [the complete five-course original-coordinate verification](docs/32X_ORIGINAL_ORDERED_ROAD_GEOMETRY_20261122.md),
[Highland's original two-level bridge](docs/ORIGINAL_32X_MODE3_ALTERNATE_BRIDGE_20261122.md),
and [the owner-only milestone record](docs/OWNER_32X_PLAYABLE_MILESTONE_20261122.md).

## November 23, 2026: original 32X landscapes performance pass

The optional original 32X landscape course build now eliminates empty 64-unit
source-road patches **before** expensive height sampling and projection.
All five owner-cartridge-derived environments drive and render faster in
matched short mGBA tests (Big Forest 18.78→20.51, Bay Bridge 19.57→24.21,
Acropolis 16.19→22.32, Highland 16.11→19.25, Sand Park 17.68→21.45
rendered game FPS). Original terrain and GBA-authored scenery remain visible;
source road/bridge collision and saves are unchanged. The private generated
five 8192-byte culling masks occupy **only GBA ROM**; no extra framebuffer,
heap, IWRAM or EWRAM. See
[original-source rendering and benchmark documentation](docs/32X_SOURCE_SCENE_ROM_CULLING_FPS_PASS_20261123.md).

This is not an authenticated port of the original SH-2 scene models or
competitive start/finish/AI rules; those remain explicitly unfinished.

## Playing it
- Open `notracin.gba` in a GBA emulator (Pizza Boy GBA or mGBA on Android).
- Turn on "skip BIOS intro" if your emulator asks. The ROM header has no Nintendo logo, so the real BIOS intro won't boot it.
- Saves need the emulator's save type set to Auto or SRAM.

## Controls
| Button | Driving | In the air |
|---|---|---|
| A | Gas | Gas (revs) |
| B | Brake, then reverse | Hold with left/right: spin |
| D-pad left/right | Steer | Light air control |
| D-pad up/down | | Flip forward / backflip |
| L | Handbrake (drift) | Hold with left/right: barrel roll |
| R | Nitro | Nitro |
| SELECT | Change camera | |
| START | Pause menu | |

Hold A during the 3-2-1 countdown to rev the engine.

## Menus
- **Title:** press START.
- **Mode select:** Arcade, Free Run, Stunt City, Load Game, Options, Records.
- **Course select** (Arcade and Free Run): left/right picks Big Forest, Bay Bridge or Acropolis. It shows the course map, difficulty, laps, length and your best lap and race.
- **Pause (START):** Resume, Reset Car, Options, Save Game, Load Game, Records, Cheats, Quit to Title.
- **Cameras:** Chase, Far Chase, Overhead, Cockpit. The chase cameras look into turns, pull back with speed, watch the loop from the side, and shake on hard landings and crashes.

## Circuits
The standard build has three custom courses inspired by Virtua Racing's corner order, hills and landmarks:

| Course | Level | What's on it |
|---|---|---|
| Big Forest | Beginner | Long right sweeper, a chicane, a downhill to the amusement park hairpin (Ferris wheel, circus tents, a hot air balloon), a little bridge over the creek |
| Bay Bridge | Medium | Harbour straight with cranes, a climb onto a suspension bridge over the bay, a city chicane and S-bends down the waterfront, a lighthouse |
| Acropolis | Expert | Climbing esses up to the temple hairpin, a long downhill, a double-apex right, a switchback and a last tight hairpin before the line |

- **Arcade:** 3 laps against seven CPU cars, starting last on the grid. The clock counts down; the half-lap checkpoint and each lap line add time. Run out and it's TIME OVER. After the flag you get your position, total time and lap times.
- **Free Run:** the course to yourself, no clock, as many laps as you like. CPU-opponent route setup and opponent simulation are skipped entirely.
- Off the road, the grass slows you right down; the barriers keep you on the circuit.
- Best laps and best race times are saved for each course.
- In a race, START pauses (Resume, Restart Race, Options, Quit to Menu).

## Things to do
- **Stunt Park lap:** cross the start line to start a lap through four gates (start, loop exit, banked curve, past the canal). An arrow points to the next gate. You get split times and a best lap, and a see-through blue ghost replays your best lap to race against. Leaving the park abandons the lap, and a wreck puts you back at the last gate.
- **Stunts and combos:** jumps score for distance and airtime, the loop for entry speed, and drifts for speed and length. Stunts chained within about 4 seconds build a combo up to x5; a crash loses it.
- **Air tricks:** flips (600), barrel rolls (500) and 360 spins (400), times the combo. Land upright or you wreck.
- **Nitro:** hold R. The meter (N, under the rev bar) refills from stunts and drifts.
- **Stars:** 22 to collect. 18 are on city streets, and 4 are in the air: over the canal, off the practice kicker, above the banked curve, and one high over the middle of town. They stay collected in your save, and all 22 pays 10,000.

## Options
| Option | Choices |
|---|---|
| Gravity | Earth, Moon, Jupiter |
| Engine | Stock, Tuned, Rocket |
| Tyres | Street, Racing, Ice |
| Crashes | On, Off |
| Game speed | Normal, Turbo, Chill |
| Air slow-mo | Off, On |
| Time of day | Day, Sunset, Night, Synthwave, Game Boy |
| Paint | Red, Blue, Green, Gold, Black, White, Pink, Rainbow |
| Camera shake | On, Off |
| Speed units | MPH, KMH |
| Ghost car | On, Off |
| HUD | Full, Clean |
| Sound | On, No Engine, Off |
| Autosave | On, Off |

## Cheat codes
Pause, choose CHEATS and press the buttons in order. Type a code again to turn it off. START leaves the screen. Cheats last until you switch off and aren't saved.

| Cheat | Code | What it does |
|---|---|---|
| Fly mode | Up, Up, Down, Down, Left, Right, Left, Right | Hold up at speed to take off. A throttle, B airbrake, up/down pitch, left/right turn. High enough, you fly over the rooftops. |
| Glitch launch | B, A, B, A, Up, Down, B, A | Hitting a wall flings the car across the map (like the GTA 4 swing). |
| Super hop | L, R, L, R, L, R, Up, Up | Press L and R together to jump. |
| Infinite nitro | R, R, R, R, Left, Left, A, A | The nitro meter never empties. |

## Saving
- The game uses the cartridge's battery save (SRAM, 32 KB).
- With autosave on, your options, records, stars and best lap (with its ghost) save automatically: on a new best lap, a new star, leaving the options screen, and quitting to the title.
- Three save slots keep the car's position and your score. Use Save Game and Load Game in the pause menu, or Load Game on the title.
- Records: best lap, high score, stunts, loops, longest jump, most airtime, top speed, crashes, time played, stars and tricks.

## Physics
- Normal-speed physics uses one step per native GBA video-frame interval (about 59.73 steps per second), independently of drawing. Each draw can catch up at most four steps; genuine overload drops whole excess steps while retaining fractional time. Turbo, Chill and air slow-mo change the step interval.
- Engine with a power curve and a six-speed automatic gearbox, brakes, reverse, air drag and rolling resistance. Top speed is about 92 mph on Stock.
- Tyre grip: past the limit the car slides. Too much steering at speed understeers, the handbrake lets the tail out, and a sliding car swings back toward its path.
- Gravity pulls you down slopes, which helps round the banked curve.
- Body pitch and roll on springs: squat, dive, lean, and tilt with ramps.
- Ramps launch the car on a real arc. Landings are judged on impact and angle: land clean, bounce or wreck.
- The loop works like a real one: below about 60 mph at the entry you fall off the top.
- Crashes: a wall above about 45 mph or a hard landing sends the car tumbling; the canal sinks it. It resets after a short wait, or use Reset Car.

## Look and sound
- Sky gradient, a two-layer city skyline that turns with the camera, and distance haze. Busy views reduce the draw distance; measured rendering rates depend on the course and physics settings.
- A detailed car with a shadow that follows ramps and banks.
- Effects: tyre smoke, burnout smoke, skid marks, dust, sparks, nitro flames, fire and black smoke after a wreck, and a splash in the canal.
- Sound on the GBA's four built-in channels: engine following the revs through the gears, tyre squeal, wind in the air and on nitro, landing thumps, crashes, splashes, and jingles for stunts, checkpoints, laps and the countdown. There is no music yet.

## Building from source
The game is C with a little ARM assembly. No devkitPro is needed.
1. Install the ARM toolchain: `sudo apt install gcc-arm-none-eabi python3`.
2. In this folder run `make`. It builds `notracin.gba`.
3. `make clean` removes the build.

The project has its own startup code (`src/crt0.s`) and linker script (`gba.ld`). Speed-critical code runs from the GBA's fast internal RAM as ARM code (marked `IWRAM_CODE`), and the linker script puts the division routines there too. `tools/fixheader.py` fills in the ROM header checksum.

### Optional original Virtua Racing courses

The standard build keeps the three custom courses and Stunt City. An optional
local import adds the original Mega Drive **Big Forest, Bay Bridge and Acropolis**
as a second course set. It reads the original meshes, native road routes,
collision surfaces and height planes from your own source ROM.

```sh
make vr-original VR_ROM="/path/to/Virtua Racing (USA).md"
```

The result is `build/vr-original/notracin-vr-original.gba`. Open it in a GBA
emulator with the existing skip-BIOS-intro setting. Choose **Arcade** or
**Free Run**, then press **L/R** on the course screen to switch between
**CUSTOM COURSES** and **VR ORIGINALS**. Left/right selects one of the three
courses in that set. Boot still starts with the custom set, and Stunt City
remains a separate choice. Original and custom best times use separate save
records; existing custom saves keep their original layout.

The first supported source is the 2,097,152-byte US revision, product
`MK-1229 -00`, with SHA-256
`291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486`.
The importer verifies the complete file before reading its tables. A different
revision needs its own verified offsets; renaming a file does not select one.

This course pack uses No Tracin's car, controls, opponents and race rules.
Original geometry is converted at one uniform scale; source colors are mapped
to the existing GBA palette and themes. Pit-lane geometry is present, while
pit-service behaviour and alternate-route race logic are future engine work.
The Mega Drive ROM itself is not modified by this build.

Generated headers, the imported build and its provenance manifest stay under
`build/`. The source repository contains the importer and runtime integration.
Keep source ROMs in `roms/` or outside the checkout. See
[the ROM research notes](docs/ROM_RESEARCH.md) for the recovered formats,
validation and the next engine milestones.

Run the source-only regression checks with `make test`. To additionally check
the verified source ROM, set `VR_ROM` when running the Python tests.

The pack skips repeated copies of identical source scenery, aligns opponents
to banks, and fixes checkpoint/finish-tick race transitions, lapped traffic
and persistent soft-body drafting. The current update also keeps controls and
physics timing consistent between draws, handles the reproduced impact-force
overflows, follows actual movement for wrong-way warnings, ranks close finishes
by crossing order, and restores earned progress on crash recovery.
See [the current driving validation](docs/DRIVING_CONSISTENCY_VALIDATION.md)
for the tested binary, harder driving runs and coverage limits,
[the traffic milestone](docs/RACE_TRAFFIC_VALIDATION.md), and
[the earlier renderer measurements](docs/ORIGINAL_COURSES_CONTINUATION.md).

### Original Mega Drive response options

The native-engine tool independently selects the original US game's drive
response and digital steering ramp. It runs the original 68000/SVP engine
and retains the original courses. Each supported combination has a verified
complete-file hash; the original steering cap and release behavior stay intact.

| Drive divisor | Steering step | Result |
|---|---|---|
| 225 | 32 | Exact original settings |
| 180 | 32 | Stronger acceleration/braking response; previous default patch |
| 225 | 48 | Quicker steering response with original drive response |
| 180 | 48 | Stronger drive response and quicker steering together |

The strict patcher verifies the original image and writes a separate output.
For the combined setting:

```sh
python3 tools/patch_vr_response.py \
  "roms/Virtua Racing (USA).md" \
  "build/vr-physics/vr-us-response180-steering48.bin" \
  --drive-divisor 180 --steering-step 48 \
  --ips build/vr-physics/vr-us-response180-steering48.ips
```

Use a Mega Drive emulator with Virtua Racing/SVP support for this `.bin` file.
Omitting selection flags retains the previous 180/32 output exactly. Restore
detects any supported modified profile without selection flags:

```sh
python3 tools/patch_vr_response.py \
  "build/vr-physics/vr-us-response180-steering48.bin" \
  "build/vr-physics/vr-us-restored.bin" --restore
```

These are measured response options; a replacement physics model remains
future work. [The selectable-control validation](docs/VR_CONTROLS_VALIDATION.md)
documents original-CPU execution, all four profiles on all three courses,
exact restoration and the observed player/demo scope. The
[two-player continuation](docs/NATIVE_TWO_PLAYER_VALIDATION.md) verifies both
controllers in the original split-screen mode across those same profiles and
courses. The
[first physics experiment](docs/VR_PHYSICS_RESEARCH.md) preserves the earlier
longitudinal-response research.

## Source files
| File | What's in it |
|---|---|
| `src/main.c` | Game loop, title and pause menus, cameras, HUD, adaptive draw distance |
| `src/render.c` | 3D polygon renderer: camera, clipping, depth sorting, filling, sky, skyline, haze, palettes and time of day |
| `src/fill.s` | Fast row filler in ARM assembly |
| `src/world.c`, `world.h` | The city, Stunt Park, ramps, height of the ground, walls and collisions |
| `src/track.c`, `track.h` | Race circuits: nearest-road lookup, heights, barriers, drawing the road, bridges and scenery, course maps |
| `src/tracks_data.h` | The three circuits, generated by `tools/mktracks.py` (`--preview` also draws PNG maps into `build/`) |
| `src/race.c`, `race.h` | Grid, CPU rivals, laps, checkpoints and the clock, race HUD, results and course records |
| `src/car.c`, `car.h` | Car physics, gearbox, tricks, cheats (fly, hop, glitch), and the car model |
| `src/game.c`, `game.h` | Scoring, combos, the lap and its gates, ghost car, stars, nitro meter, records |
| `src/menu.c`, `menu.h` | Options, menu screens, cheat code screen, and saving to SRAM |
| `src/hud.c`, `hud.h` | Sprite HUD: text, rev bar, gauges, arrow, particles |
| `src/fx.c`, `fx.h` | Particles and skid marks |
| `src/sound.c`, `sound.h` | Sound effects, engine and jingles |
| `src/font.h`, `src/sintab.h` | 5x7 font and sine table |
| `src/gba.h` | Hardware registers and the renderer interface |
| `attic/` | Older versions kept for reference (milestones 1 and 2, earlier renderer and main loop) |

## Status and what's next
Done: the 3D engine and city, stunt physics, the look and feel pass, options and saves, tricks, nitro, stars and cheats.

The optional original-course pack adds source meshes, native terrain and road
routes while retaining No Tracin's driving and race systems. See the
[current validation notes](docs/RACE_TRAFFIC_VALIDATION.md) for measured
performance and the limits of the current testing. Independently selectable
[native Mega Drive steering and drive responses](docs/VR_CONTROLS_VALIDATION.md)
are also implemented and tested.

Planned next:
- Course expansion: larger track formats, alternate routes and functional pits.
- City life: traffic, pedestrians and cops that chase you.
- A Payback-style HUD with hearts, lives and a minimap.
- Missions with a city map, and save slots for progress.
- Music.

### Experimental classic driving profiles and upcoming Sega 32X support

**Options → HANDLING** now offers **SOFT-BODY**, **ARCADE**, **HARD STYLE** and
**RACE STYLE**. The last two use separate lightweight GBA-native steering,
slip and yaw response models inspired by early 3D driving simulators. They
are **not** a port of the original Atari Hard Drivin'/Race Drivin' ROM code;
that original code has not been recovered. The existing modes and system-save
layout are unchanged. See [32X and classic physics roadmap](docs/32X_AND_CLASSIC_PHYSICS_ROADMAP.md)
for proven capabilities and the remaining code-research work.

The supplied Sega Genesis/SVP import and its three original courses remain
unchanged. The Sega **32X Virtua Racing Deluxe** ROM has now been supplied
and its US cartridge header and full-file hash independently verified, but its
course geometry is **not imported yet**. Use the read-only
`python3 tools/audit_uploaded_cartridges.py --require-known /path/to/your/rom.32x`
to verify the exact image without rewriting it. Deluxe's two extra courses, alternate roads,
three vehicle classes and additional scenery will require a separate
version-verified importer and updated course-selection data structures.

### Independent Motion Physics — gravity, loops and falling

Options now has **two independent settings**, not four interchangeable
physics engines. **HANDLING** (Soft-Body / Arcade / Hard Style / Race Style)
changes steering and tires. **MOTION PHYS** (Original / Newton) changes
stunt dynamics. The Newton option uses radial support on loops
`N/m = v^2/R + g cos(theta)`: slow cars lose contact and **fall ballistically**
without a scripted wreck, while fast cars stay attached. Arcade-model landing
response uses a surface-normal impulse, preserving tangential momentum on
slopes. Soft-Body already has its own full node-and-spring ground contacts.
Normal driving and the original option retain their previous behavior.

This is **our new Newton-inspired physics implementation**, not an exact
copy of the original Race Drivin' physics routines. The supplied 1993 US
Sega Genesis Race Drivin' ROM is verified and preserved; its 68000 code is
not yet traced to the original Atari car dynamics. The supplied 32X Deluxe
cartridge is also identified, but its additional courses await format
analysis. See [Motion Physics and ROM validation](docs/NEWTON_MOTION_AND_ROM_INPUTS.md).

### 32X and Race Drivin' ROM tracing: verified first-stage data

The user-supplied US Virtua Racing Deluxe 32X cartridge now has an audited
**six-slot normal plus one-slot alternate** 68000 road lookup. An important
correction to the early investigation: data pointers map through the
`$900000–$9FFFFF` window with file offsets `address - $800000`, not the
code window `address - $880000`. The original offset counts were invalid;
the corrected tool verifies six coherent ROM-derived road outlines and
1,770 count-delimited records containing 13,096 undecoded 14-word subrecords
in five normal slots plus the alternate slot. Normal slot 5 does not use
that record grammar and remains unresolved. The read-only tools are
`tools/inspect_vrd_32x_tables.py` and `tools/inspect_vrd_32x_records.py`. This is *not yet* a five-course 3D
geometry importer, and the table slots are not labeled as tracks without
runtime verification. The verified Race Drivin' Genesis image can be
inspected with `tools/trace_racedrivin_genesis.py` to inventory tentative
68000 math/call sites; no original Atari physics routine is claimed yet.

The independently selectable **Motion Physics: Newton** now also conserves
the loop's lateral tangent velocity and residual movement on both forward
completion and backward exit. It keeps the prior behavior for **Original**
motion. See [ROM tracing and Newton validation](docs/ROM_TRACING_AND_LOOP_MOMENTUM_20261122.md).

### 32X Deluxe road-region and terrain-height decoding (source-only)

The read-only 32X inspector has advanced beyond counted records. A tested
interpreter now follows the exact 68000 **BSP road-boundary decisions and
surface height plane equations**, sampling all five verified normal lookup
slots without guessing course labels. A separate offline exporter creates
private, triangulated *study* road surfaces from the user-owned ROM. This is
**not an imported playable five-course Deluxe edition**: the actual source
mesh, route progression, start lines, scenery and vertical-unit conversion
still require further work. The standalone ROMs and both Handling and Motion
Physics settings remain unchanged in this source-only milestone. Details:
[32X BSP and height research](docs/32X_BSP_HEIGHT_SURFACE_DECODE.md).

### Optional five-environment 32X road-study free-drive build

The **private** `make vr-deluxe-study VR_ROM=... VR32X_ROM=...` build now adds
**32X ROAD STUDIES** as a third course set. Press L/R in Arcade or Free Run
to select it, then choose SLOT 0 through SLOT 4. The game switches these
study environments to FREE DRIVE, with no fictional lap or race records.
Their 2D road masks, sampled height planes, terrain collision and simple
bounded road polygons are derived from the exact privately supplied US 32X
ROM; their staging loops and vertical scaling are experimental. **These are
not authentic, named Highland/Sand Park/etc. complete ports**. Standard and
Genesis-only builds need no 32X data, scientific Python packages, heap or
additional framebuffer. See
[the 32X road-study implementation and test report](docs/32X_GBA_ROAD_STUDY_VALIDATION.md).

The new five 32X studies also passed 10 additional real mGBA controller
smoke tests with **Newton Motion** paired independently with **Arcade** and
**Race Style** handling on all five slots. These do not establish original
Sega 32X car physics or completed competitive races.
