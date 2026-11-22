# Native Mega Drive steering and drive response

This continuation characterizes two independently selectable coefficients in
the original Virtua Racing (USA) Mega Drive / Genesis code. The supported
settings are drive divisor **225 or 180**, and steering step **32 or 48**.
The existing 180/32 experiment remains byte-for-byte identical. The steering
change uses two signed table words and retains the original target limits,
averaging instructions, release behavior and direction-swap handling.

The original 68000 game and SVP renderer execute in Genesis Plus GX. The host
probe supplies controller input or controlled routine-call fixtures and reads
the result; it does not substitute steering, drive or rendering equations.

## Selecting and restoring a response

From the repository root, use the strict patcher with an original supported
input and a new output path:

```sh
# Original drive response, quicker steering.
python3 tools/patch_vr_response.py "roms/Virtua Racing (USA).md" \
  build/vr-physics/vr-drive225-steer48.md \
  --drive-divisor 225 --steering-step 48 \
  --ips build/vr-physics/vr-drive225-steer48.ips

# Stronger drive response and quicker steering.
python3 tools/patch_vr_response.py "roms/Virtua Racing (USA).md" \
  build/vr-physics/vr-drive180-steer48.md \
  --drive-divisor 180 --steering-step 48 \
  --ips build/vr-physics/vr-drive180-steer48.ips

# Restore any of the three supported modified images into a new file.
python3 tools/patch_vr_response.py \
  build/vr-physics/vr-drive180-steer48.md \
  build/vr-physics/vr-restored.md --restore \
  --ips build/vr-physics/vr-drive180-steer48-restore.ips
```

Omitting both selectors retains the previous 180/32 output exactly.
`--drive-divisor 225 --steering-step 32` makes an unchanged control copy.
Restore automatically identifies a supported modified profile; omit both
selectors when restoring. Applying requires the exact original input, so
choose another profile from that original rather than stacking patches.

The tool checks complete image hashes, instruction guards and cartridge
checksums, refuses existing output paths and source/output aliases, and
never writes the input. The normal IPS files cannot enforce a source hash;
use the Python tool for those checks. The drive-only and steering-only IPS
files are 22 bytes each; the combined IPS is 36 bytes.

## Supported image and exact profiles

The source is the same unheadered 2,097,152-byte USA image, product header
`MK-1229 -00`, used by the earlier response experiment.

| Drive divisor | Steering step | SHA256 | Header checksum | Bytes differing from the source |
|---:|---:|---|---|---|
| 225 | 32 | `291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486` | `95AB` | None |
| 180 | 32 | `c12cb153b7e82854c2e6d23abbe24d981d34a69c5f07a8370fc228bc17738cc3` | `957E` | `018F`, `0295D1` |
| 225 | 48 | `5c70dafd5dadc84f9a0bcacfd52e75605b67c09c92953cc56e63150be123c4ca` | `95AB` | `02961D`, `029621` |
| 180 | 48 | `6a2a7b06eef9850a803cb2240a5c28b5ddb188612a16203905fef2609b96d367` | `957E` | `018F`, `0295D1`, `02961D`, `029621` |

Steering alone does not change the cartridge checksum: the sum of the two
signed table words is unchanged modulo 65536. The combined profile needs
the same checksum correction as the original drive-only experiment.

### Minimal steering edit

| ROM word offset | Original | Quicker | Meaning |
|---|---|---|---|
| `02961C` | `FF E0` (−32) | `FF D0` (−48) | Negative target step |
| `02961E` | `00 00` | Unchanged | Neutral table entry |
| `029620` | `00 20` (+32) | `00 30` (+48) | Positive target step |

The routine enters at `029622`. Its `LEA` at `029660` selects the neutral
table word, and a signed index chooses the negative, neutral or positive
entry. On a direction change it seeds the global target at `FFD000` and the
car's steering state at `+90` immediately. On continued hold it adds the
selected step to the target. Original instructions clamp the target to
`−127..127`, then average the existing steering state with `target × 256`.

The following original bytes are useful structural guards in addition to
the full-image SHA256 check:

| Offset | Original bytes | Invariant |
|---|---|---|
| `0295CE` | `83 FC 00 E1` | Drive `DIVS.W #225,D1` |
| `02961C` | `FF E0 00 00 00 20` | Complete original signed step table |
| `029660` | `43 FA FF BC` | PC-relative reference to `02961E` |
| `0296AE` | `0C 78 00 7F D0 00` | Positive target limit comparison |
| `0296B6` | `31 FC 00 7F D0 00` | Positive target clamp |
| `0296BC` | `0C 78 FF 81 D0 00` | Negative target limit comparison |
| `0296C4` | `31 FC FF 81 D0 00` | Negative target clamp |
| `0296D6` | `0C 40 00 20` | Existing target deadband comparison |
| `0296EC` | `E2 82` | Existing arithmetic averaging shift |
| `02971E` | `0C 41 00 20` | Existing final steering-state deadband |

Only the table's negative and positive words change for steering 48. The
drive option continues to change only the immediate word at `0295D0`, from
225 to 180, plus the header checksum.

## Measured steering behavior

The following values come from execution of the donor's actual 68000
routine. Positive denotes action mask `0x04` (normally left); negative is
its opposite, mask `0x08`. These are native internal units, not degrees.

| Held-direction update | Original target | Quicker target | Original steering | Quicker steering |
|---:|---:|---:|---:|---:|
| 1 | 32 | 48 | 8192 | 12288 |
| 2 | 64 | 96 | 12288 | 18432 |
| 3 | 96 | 127 | 18432 | 25472 |
| 4 | 127 | 127 | 25472 | 28992 |
| 5 | 127 | 127 | 28992 | 30752 |
| 6 | 127 | 127 | 30752 | 31632 |
| 7 | 127 | 127 | 31632 | 32072 |
| 8 | 127 | 127 | 32072 | 32292 |
| 9 | 127 | 127 | 32292 | 32402 |
| 10 | 127 | 127 | 32402 | 32457 |
| Long hold | 127 | 127 | 32511 | 32511 |
| First release update | 0 | 0 | 0 | 0 |

The negative direction follows the negative of the displayed early values.
After a sufficiently long hold, both profiles settle at **+32511 or −32512**.
That one-unit asymmetry comes from the original signed arithmetic shift;
the steering patch preserves it. The target maximum remains exactly ±127.

The 48 setting increases the initial target and steering state by 50%, and
reaches the target cap in three updates instead of four. It crosses 90%
of the native steering scale in update five instead of six, and 99% in
update eight instead of nine. This measures a quicker onset; it is not
a claim that the car has a greater maximum steering angle or better lap
times.

Release immediately yields zero in both profiles. Holding both directions
also yields zero. Directly reversing a held direction immediately seeds
the opposite state: −8192 for the original step or −12288 for step 48,
followed by its normal ramp. This retains the original immediate reversal
behavior, with a larger first step.

Bit 7 at `FFFDC8` swaps the direction bits. Both settings were verified
with this bit clear and set. Its game-mode meaning is not assumed here.

### Speed and course invariance

The steering routine itself does not read the car's speed. The call matrix
nevertheless verifies identical steering output at internal speeds
**0, 300, 600, 900 and 1200**, with the corresponding converted-speed field
set consistently. It uses original countdown RAM captured separately on
Beginner, Medium and Expert. Each input history restores deterministic
CPU state and the captured RAM before entry; the supplied PC, A0, stack
and SR establish an isolated native call.

Seven 64-update histories cover neutral, left hold/release, right
hold/release, both directions, direct reversals, transitions through both
directions, and alternating inputs. Five speeds, two direction-swap flag
states and seven histories produce 4480 executions for each course/profile.
All 53,760 steering calls returned successfully and satisfied the limits,
release, inversion and independence assertions.

The complete supported-profile native-call suite consists of:

| Routine family | Executions |
|---|---:|
| Steering | 53,760 |
| Longitudinal response | 360 |
| Throttle/brake ramps | 576 |
| **Total** | **54,696** |

For identical steering inputs, changing the drive divisor produces
byte-identical steering-call CSVs. For identical drive inputs, changing
the steering step produces byte-identical speed-call CSVs. Throttle and
brake ramp outputs remain identical for every profile. These checks
establish that the two selected coefficients can be configured separately.

### Original downstream coupling remains

The steering routine also updates the existing `+AE` change accumulator.
In isolated calls without the later turn routine's decay, a long hold
accumulates 90 originally and 74 with the quicker step. In ordinary
Beginner gameplay the first pulse peaks at 56 and 48 respectively, because
the subsequent routine at `0298C2` decays this state by eight per update.
Code at `0298CC` compares it to 80 when choosing how to update the original
handling coefficient at `+78`.

The experiment preserves that mechanism. It does not claim that changing
steering response leaves every downstream handling state or trajectory
identical. The original turn routine also consumes steering and speed;
after steering begins, position, contact, handling and speed can naturally
diverge. The pre-steering portion of each course run is byte-identical
between the two steering settings at a fixed drive divisor.

## Native controller-driven course validation

Every one of the four profiles boots normally and selects each of the
three courses using controller input. Each observed run covers **5501
video frames**, including **3397–3525 active-driving frames**. The script
accelerates, brakes, releases, holds and releases each direction, reverses
directly from left to right, then holds both directions before continuing.
It never writes car RAM during these gameplay runs.

All 12 runs stay in the expected one-player race loop and selected course,
continue issuing original SVP race-render commands, and record no stopped
CPU. Frame-5400 captures of the combined profile show the original course
and HUD on Beginner, Medium and Expert. These scripted runs exercise the
controls; they are not competitive driving or complete-race balance tests.

Early drive measurements remain identical for steps 32 and 48:

| Course | Divisor 225: frames to speed 600 | Divisor 180: frames to speed 600 | Divisor 225: speed at frame 120 | Divisor 180: speed at frame 120 |
|---|---:|---:|---:|---:|
| Beginner | 135 | 107 | 539 | 679 |
| Medium | 152 | 121 | 473 | 594 |
| Expert | 158 | 130 | 459 | 549 |

The full-brake delta at run frame 240 is −32 for divisor 225 and −40
for divisor 180, on every course with either steering setting.

Instruction observations record the native steering output just before its
store at `029726`. In every course pair the quicker setting reaches target
127 **one game update, or four observed video frames, earlier** during
the first steering pulse. This is an observed interval, not a general
promise that the game always runs one physics tick per four video frames.
The scope records confirm zero on release and on both-directions input,
and the appropriate −32/−8192 or −48/−12288 countersteer seed.

### Observed shared-use scope

Across the 12 observed runs the steering entry is reached 10,906 times:
10,336 in the race loop `0248AC` and 570 in the attract loop `02550A`.
Every observed steering call has A0=`FF9000`, the player/demo record.
The observed return addresses are `026658`, `02677E` and `026EE8`.
The separate opponent speed path at `02A302` is observed on all 15
opponent records `FF9100..FF9F00`; no opponent record reaches the patched
steering routine in these runs.

The patch has no car-ID or mode guard. All uses of the shared steering
routine receive the selected setting. Recorded replay and two-player
behavior are not independently playtested. Old input replays should not
be presumed to reproduce the original trajectory under changed settings.

The existing core instruction hook is read-only. Each observed course
run has an unobserved twin, and the complete per-frame gameplay CSV is
**byte-identical in all 12 pairs**. This checks that instrumentation did
not alter the measured simulation.

## Rejected lower-step candidate

A table-only step 16 was executed before narrowing the supported choices.
It fails the intended symmetric behavior: the positive target is cleared
by the original threshold logic and the held steering remains +4096,
while the negative side continues to ramp to −127/−32512. The small
`rejected-step16-evidence.csv` records this native result. Supporting 16
would require changing additional logic; that is outside this minimal
table-only experiment. The supported steering choices are only 32 and 48.

## Reproducing and handling evidence

Unpack the separately delivered `vr-native-controls-research.zip` evidence
archive and open a terminal in the directory containing `validate_controls.py`,
`build_probe.py` and the two C probes. These research programs are included in
that archive; a fresh No Tracin' source checkout alone does not contain them.

Use the already-built original core from [VirtuaRacingMDEnhanced commit
`8004693ed487c7ad74b3f3e560780a5f3180287d`](https://github.com/PokaMilkTea/VirtuaRacingMDEnhanced/tree/8004693ed487c7ad74b3f3e560780a5f3180287d). The probe links its standard
68000/SVP path; it does not enable the host GPU renderer or SVP overclock.

```sh
python3 validate_controls.py \
  --rom "/path/to/Virtua Racing (USA).md" \
  --research "/path/to/VirtuaRacingMDEnhanced" \
  --output "/path/to/new-private-output-directory"
```

The output directory must be new. The runner checks the source hash,
constructs three exact derived fixtures, captures fresh original RAM,
builds the probes, performs 12 observed plus 12 unobserved course runs,
executes the native-call matrix and writes `validation-summary.json`.
A clean end-to-end rerun passed the assertions and reproduced the results.
The source hash is checked again at the end.

The portable evidence consists of `physics_probe.c`, `physics_calls.c`,
`build_probe.py`, `validate_controls.py`, this document, the small rejected
candidate CSV, the successful summary and the generated observation CSVs.
Exclude the output's `private/` directory, ROMs, RAM captures, screenshots,
compiled probes and third-party emulator files from source control or a
public research archive. The strict production patcher is maintained in
the No Tracin' repository separately.

The remaining limits are physical Mega Drive validation, complete race
balance and collision coverage, replay and two-player testing, and the
separate work needed for custom Mega Drive courses or a replacement
physics model. These results establish bounded native response options.
