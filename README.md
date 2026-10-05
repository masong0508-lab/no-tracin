# No Tracin'

A stunt-driving game for the Game Boy Advance: Race Drivin' (Genesis) mixed with Payback (GBA) and a bit of Virtua Racing. You drive a flat-shaded 3D city, Freedom City, with a Stunt Park (a loop, a banked curve and a canal jump), street ramps, tricks, stars to find and cheat codes.

Everything in the game is drawn and played by its own code. No code, graphics or sound from either original ROM is in it, so `notracin.gba` can be shared. The original ROMs you own are not part of this folder and should not be shared.

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
- **Title:** Drive, Load Game, Options, Records.
- **Pause (START):** Resume, Reset Car, Options, Save Game, Load Game, Records, Cheats, Quit to Title.
- **Cameras:** Chase, Far Chase, Overhead, Cockpit. The chase cameras look into turns, pull back with speed, watch the loop from the side, and shake on hard landings and crashes.

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
- Fixed 60 steps per second whatever the frame rate, so jumps and the loop behave the same at any speed of drawing.
- Engine with a power curve and a six-speed automatic gearbox, brakes, reverse, air drag and rolling resistance. Top speed is about 92 mph on Stock.
- Tyre grip: past the limit the car slides. Too much steering at speed understeers, the handbrake lets the tail out, and a sliding car swings back toward its path.
- Gravity pulls you down slopes, which helps round the banked curve.
- Body pitch and roll on springs: squat, dive, lean, and tilt with ramps.
- Ramps launch the car on a real arc. Landings are judged on impact and angle: land clean, bounce or wreck.
- The loop works like a real one: below about 60 mph at the entry you fall off the top.
- Crashes: a wall above about 45 mph or a hard landing sends the car tumbling; the canal sinks it. It resets after a short wait, or use Reset Car.

## Look and sound
- Sky gradient, a two-layer city skyline that turns with the camera, and distance haze. It holds 30 fps; on the busiest views the draw distance pulls in a little and the haze hides the edge.
- A detailed car with a shadow that follows ramps and banks.
- Effects: tyre smoke, burnout smoke, skid marks, dust, sparks, nitro flames, fire and black smoke after a wreck, and a splash in the canal.
- Sound on the GBA's four built-in channels: engine following the revs through the gears, tyre squeal, wind in the air and on nitro, landing thumps, crashes, splashes, and jingles for stunts, checkpoints, laps and the countdown. There is no music yet.

## Building from source
The game is C with a little ARM assembly. No devkitPro is needed.
1. Install the ARM toolchain: `sudo apt install gcc-arm-none-eabi python3`.
2. In this folder run `make`. It builds `notracin.gba`.
3. `make clean` removes the build.

The project has its own startup code (`src/crt0.s`) and linker script (`gba.ld`). Speed-critical code runs from the GBA's fast internal RAM as ARM code (marked `IWRAM_CODE`), and the linker script puts the division routines there too. `tools/fixheader.py` fills in the ROM header checksum.

## Source files
| File | What's in it |
|---|---|
| `src/main.c` | Game loop, title and pause menus, cameras, HUD, adaptive draw distance |
| `src/render.c` | 3D polygon renderer: camera, clipping, depth sorting, filling, sky, skyline, haze, palettes and time of day |
| `src/fill.s` | Fast row filler in ARM assembly |
| `src/world.c`, `world.h` | The city, Stunt Park, ramps, height of the ground, walls and collisions |
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

Planned next:
- Race mode: a countdown timer that checkpoints extend, and a speed track through the city.
- City life: traffic, pedestrians and cops that chase you.
- A Payback-style HUD with hearts, lives and a minimap.
- Missions with a city map, and save slots for progress.
- Music.
