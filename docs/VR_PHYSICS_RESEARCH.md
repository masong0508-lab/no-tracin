# Original Mega Drive physics: verified response experiment

> **Historical checkpoint:** This document records the original fixed 180/32
> drive-response experiment. The patcher's default remains byte-for-byte
> identical. See [Native controls validation](VR_CONTROLS_VALIDATION.md) for
> the later independently selectable drive 225/180 and steering 32/48
> settings, four-profile evidence, shared-use scope and current limits.

This checkpoint changes the original **Virtua Racing (USA)** Mega Drive engine.
The original 68000 game code and SVP renderer execute in Genesis Plus GX; the
experiment changes one immediate operand in the game's speed update. It is
separate from No Tracin's optional import of the original courses into its GBA
engine.

**Result:** a reversible response-divisor patch produces measurable acceleration
and braking changes, boots through normal menus, and runs all three original
courses. It establishes a tested route for further native-engine work. It does
not yet implement a replacement physics model, new Mega Drive courses, or a
larger-map engine.

## Supported image and exact change

All offsets and results below apply to this one unheadered image:

| Property | Value |
|---|---|
| Title / region | Virtua Racing (USA), Mega Drive / Genesis |
| Size | 2,097,152 bytes |
| Product header | `MK-1229 -00` |
| Original SHA256 | `291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486` |
| Patched SHA256 | `c12cb153b7e82854c2e6d23abbe24d981d34a69c5f07a8370fc228bc17738cc3` |
| Original / patched checksum | `0x95AB` / `0x957E` |

The instruction at ROM/68000 address `0x0295CE` is
`DIVS.W #225,D1` (`83 FC 00 E1`). The experiment changes its immediate word to
180, producing `DIVS.W #180,D1` (`83 FC 00 B4`).

| Word offset | Original | Patched | Reason |
|---|---|---|---|
| `0x0295D0` | `00 E1` | `00 B4` | Longitudinal response divisor: 225 to 180 |
| `0x00018E` | `95 AB` | `95 7E` | Correct the cartridge checksum |

Only **two bytes differ** in the entire image: `0x0295D1` and `0x00018F`.
Each forward/restore IPS contains two ordinary records and is **22 bytes**.
The source ROM is never overwritten. Restoration reproduces its original
SHA256 and its complete contents byte for byte.

### Why this affects driving

The surrounding original instructions are:

```text
0295C8  MOVE.W D2,D1
0295CA  ASR.W  #2,D1
0295CC  EXT.L  D1
0295CE  DIVS.W #225,D1
0295D2  MOVE.W D1,$0C(A0)
0295D6  ADD.W  D1,$06(A0)
0295DA  BPL    $0295E0
0295DC  CLR.W  $06(A0)
```

The routine has already combined the game's drive, drag, braking and other
terms before this point. The signed quotient is the speed change for this
update; it is stored at car offset `+0x0C` and added to the speed at `+0x06`.
Negative resulting speed is clamped to zero. Original position integration at
`0x027962` subsequently uses `+0x06`.

Changing the divisor scales this response by a nominal `225 / 180 = 1.25`
before integer truncation. Both positive and negative speed changes are
affected. The original surrounding limits, gear changes, terrain effects and
integer rounding still apply. This is a response coefficient, and the research
does not assign it a physical mass or claim a particular top-speed increase.

## Applying and restoring

Use the strict patcher to check the revision, instruction, checksum and result:
Run these examples from the repository root. They keep the input image under
the ignored `roms/` directory and generated files under `build/vr-physics/`.
An absolute path outside the checkout can also be used for the input.

```sh
python3 tools/patch_vr_response.py \
  "roms/Virtua Racing (USA).md" \
  "build/vr-physics/vr-response180.md" \
  --ips build/vr-physics/vr-us-response180.ips
```

To reconstruct the exact original into a new file:

```sh
python3 tools/patch_vr_response.py \
  "build/vr-physics/vr-response180.md" \
  "build/vr-physics/vr-restored.md" \
  --restore --ips build/vr-physics/vr-us-response180-restore.ips
```

The patcher rejects a wrong revision, a headered or truncated image, repeated
application, restoration of an unmodified image, and any existing output path.
It also refuses source/output aliases. An IPS file itself cannot enforce a
source SHA256, so the Python patcher provides the revision checks.

## Located controls and state updates

The following map comes from disassembly of the supported image, direct calls
into its original routines, controller-driven emulation and instruction-hook
observations. A **tick** means one execution of the relevant game update;
measurements below count emulated video frames separately. The upstream
[game-loop research][game-loop] explains the race's phase scheduling and SVP
wait periods.

| Original entry | Characterized role | Evidence and boundary |
|---|---|---|
| `0x029622` | Digital direction to steering target and smoothed steering state | Executed directly for left, right, both and neither, including release |
| `0x02972C` | Throttle and brake command ramps | Executed directly for six action masks and observed under normal B/A button input |
| `0x0294E2` | Longitudinal speed update | Direct execution with 90 input states per version; patched division observed before execution |
| `0x0293A0` | Gear/engine-state management | Disassembly maps gear state and ratio-table use; not independently replaced |
| `0x029796` | Steering/speed-dependent turn update | Disassembly reads steering and handling state and updates heading; no handling patch applied |
| `0x027962` | Heading and speed to X/Z movement, followed by contact handling | Disassembly connects `+0x06` to the position fields |
| `0x02A302` | Separate opponent speed update | Observed on all 15 opponent records in every tested race and attract run |

The player record is at 68000 RAM `0xFF9000`. Car records are spaced by `0x100`;
the observed opponent records are `0xFF9100` through `0xFF9F00`.

| Offset from car record | Meaning established here |
|---|---|
| `+0x04` | Converted speed: `(+0x06 * 54) >> 8` in routine `0x029A24` |
| `+0x06` | Internal speed consumed by movement |
| `+0x0C` | Signed speed delta from the longitudinal update |
| `+0x0E` | Propulsion command, clamped to `-51..255` |
| `+0x10` | Brake command, clamped to `0..255` |
| `+0x2E`, `+0x30`, `+0x32` | X, height and Z state |
| `+0x3A`, `+0x3E` | Heading and movement-heading state |
| `+0x76` | Engine-speed state used to index the drive tables |
| `+0x7C` | Gear state |
| `+0x90` | Signed steering state |

### Steering behavior

The global steering target at `0xFFD000` moves in 32-unit steps and clamps at
`+127` or `-127`. On a direction change, the routine sets the target to
`+32`, `-32` or zero and seeds the steering state immediately. Subsequent
same-direction updates average the old steering state with `target * 256`.

Direct original-CPU execution of a left hold produced:

| Update | Target | Steering at `+0x90` |
|---|---:|---:|
| 1 | 32 | 8192 |
| 2 | 64 | 12288 |
| 3 | 96 | 18432 |
| 4 | 127 | 25472 |
| 5 | 127 | 28992 |
| 6 | 127 | 30752 |
| Release | 0 | 0 |

Right is the signed mirror. Holding both directions produces zero. These
results were identical with the response patch. This identifies concrete
places to study steering feel while preserving a measured reference.

### Throttle and braking behavior

Action bit `0x10` raises `+0x0E` by 64 per eligible update, up to 255; release
lowers it by 64 to a minimum of -51. Action bit `0x40` raises `+0x10` by 64
to 255; release lowers it to zero. Certain car-state conditions bypass or
override the throttle update.

In normal default-controller runs, B produced action `0x10` and A produced
action `0x40`. The propulsion value multiplies a drive-table result inside
`0x0294E2`; it is therefore a control input to the speed calculation. The
brake value contributes a term based on constant `0x71C0`, subtracted before
the final response division. These ramp routines were unchanged by the patch.

## Exactly which cars use the patched path?

The research build enabled Genesis Plus GX's existing
[instruction callback][cpu-hook] and recorded the address, A0, current main
loop and stack return address whenever either `0x0295CE` or `0x02A302` ran.
No hook changed a register, memory location, input or emulated cycle count.

Across original/patched pairs on all three courses:

| Observed path | A0 records | Observed calls |
|---|---|---:|
| Patched division, one-player race loop `0x0248AC` | `0xFF9000` | 5,078 |
| Same division, attract loop `0x02550A` | `0xFF9000` | 285 |
| Separate opponent update `0x02A302`, race and attract | All 15 records `0xFF9100..0xFF9F00` | 84,536 |

Thus the changed division was reached **5,363 times by the player/demo record**,
and no opponent record reached it in the tested runs. Ordinary opponents used
the separate update at `0x02A302`. Their behavior can still change indirectly
through interactions with the player's altered position and speed.

The patch has no player-ID or game-mode guard. It changes every use of this
integrator, including the attract driver. Recorded replays and two-player
mode were not separately playtested. The original replay mechanism feeds
recorded inputs back into the simulation, so results recorded under the old
physics should not be assumed to reproduce under the patch.[game-loop]

For every course/version pair, the observer-enabled probe produced a
**byte-identical per-frame gameplay CSV** to the ordinary probe. This checks
that the observations did not change the measured simulation.

## Measured effects in the actual game

Each original/patched pair starts at power-on and uses controller input to
navigate the menus and select its course. The runner observes menu state
instead of assuming fixed title-demo timing, because the patch changes the
demo driver's behavior too. It does not write car state during these runs.

Once the game enters its driving state, the same sequence is applied relative
to that start: accelerate, brake, release, left and right steering pulses, then
continue accelerating. Each run covers **5,501 emulated video frames**, with
at least **3,397 frames after driving becomes active**. All six remain in the
expected race loop/course, continue issuing SVP race commands, and have no
observed stopped CPU. Screenshots at frame 5,400 show the original rendered
course and HUD on all three patched runs.

Speeds in this table are the original internal `+0x06` units; frame 0 is the
observed start of driving, not power-on.

| Course | Frames to speed 600, original | Frames to speed 600, patched | Speed at frame 120, original | Speed at frame 120, patched |
|---|---:|---:|---:|---:|
| Beginner | 135 | 107 | 539 | 679 |
| Medium | 152 | 121 | 473 | 594 |
| Expert | 158 | 130 | 459 | 549 |

During the scripted full-brake interval, the speed delta at frame 240 is
**-32 originally and -40 patched on every course**. These are ordinary game
updates under controller input, as well as independently checked results
from isolated routine calls.

These tests establish boot and continued course execution plus the intended
response change. They do not establish complete race balance, all collision
cases, replay compatibility, two-player behavior, or operation on physical
Mega Drive hardware.

### Controlled original-CPU calls

Each isolated case restores a captured original countdown RAM state and CPU
state, supplies specified car/control fields, and executes the original
routine until its return sentinel. No host replacement computes the result.
The observer records D1 immediately before the patched division.

- 90 longitudinal cases per ROM: three courses, five starting speeds, three
  propulsion levels and two brake levels.
- 48 control-ramp updates per ROM.
- 40 steering updates per ROM.
- **356 native routine executions in total**, all returned successfully.

For all 90 longitudinal input pairs, the dividend reaching the instruction was
identical between versions. The resulting deltas exactly matched signed
division by 225 or 180, with truncation toward zero. Control-ramp and steering
results remained identical.

Example at starting speed 300, gear state 1 and engine-speed state 4000:

| Control | Original delta | Patched delta |
|---|---:|---:|
| Full propulsion, no brake | +16 | +20 |
| Released propulsion, no brake | -10 | -13 |
| Full brake | -32 | -40 |

## Evidence checkpoint and reproduction

The independent `vr-physics-research` checkpoint contains:

- `physics_probe.c`: controller-only boot/gameplay runner with RAM observations
  and an optional existing-core instruction hook.
- `physics_calls.c`: isolated original-68000 routine executor.
- `build_probe.py`: links these probes against existing Genesis Plus GX objects.
- `validate_experiment.py`: builds the probes, reproduces all six course runs,
  checks the native routine cases, validates IPS application/restoration and
  writes `validation-summary.json`.
- The strict patcher, two 22-byte IPS files, per-frame/native-call observation
  CSVs and the summarized instruction-scope evidence.

The checkpoint excludes ROM images, RAM snapshots, screenshots, emulator
binaries and third-party core source. Its measurements describe execution;
it does not contain extracted course meshes or commercial asset banks. A
separate patched ROM copy is retained privately for testing.

The emulator/core checkout used was
[VirtuaRacingMDEnhanced commit `8004693`][research-commit]. Its `src/core.c`
initializes the original Genesis Plus GX 68000/SVP path. The probe does not
use the project's host GPU renderer or enable its SVP overclock.

After following that checkout's build instructions to produce its core objects,
run from the checkpoint directory on a Linux host with Python 3 and a C compiler:

```sh
python3 validate_experiment.py \
  --rom "/path/to/Virtua Racing (USA).md" \
  --research "/path/to/VirtuaRacingMDEnhanced" \
  --output "/path/to/new-private-validation-directory"
```

The output directory must be new. The script creates its patched test image
and RAM snapshots inside that private directory; it does not alter the input
image. A clean end-to-end rerun reproduced the reported measurements.

Repository patcher tests can be run without a commercial fixture, with the
additional donor checks enabled when the original image is supplied:

```sh
python3 -m unittest discover -s tests -p 'test_vr_response_patch.py' -v
VR_ROM="/path/to/Virtua Racing (USA).md" \
  python3 -m unittest discover -s tests -p 'test_vr_response_patch.py' -v
```

## Next native-engine work this enables

The original driver and opponent paths can now be distinguished and measured.
A steering-response experiment can use the same direct-call and course-run
checks, while a drive-table or gearing change can retain the original update
timing. A configurable overhaul should decide which modes share each change,
how replays are versioned, and whether opponent behavior should follow the
same model. Custom Mega Drive geometry remains a separate engine/data-format
task; the GBA optional-course importer is not evidence that those limits have
already been expanded in the original cartridge engine.

[game-loop]: https://github.com/PokaMilkTea/VirtuaRacingMDEnhanced/blob/8004693ed487c7ad74b3f3e560780a5f3180287d/docs/game-loop.md
[cpu-hook]: https://github.com/PokaMilkTea/VirtuaRacingMDEnhanced/blob/8004693ed487c7ad74b3f3e560780a5f3180287d/third_party/genplus-gx/core/m68k/m68kcpu.c
[research-commit]: https://github.com/PokaMilkTea/VirtuaRacingMDEnhanced/tree/8004693ed487c7ad74b3f3e560780a5f3180287d
